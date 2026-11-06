/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AccessibleCaretManager_h
#define AccessibleCaretManager_h

#include "AccessibleCaret.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Attributes.h"
#include "mozilla/EnumSet.h"
#include "mozilla/EventForwards.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/CaretStateChangedEvent.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "nsCOMPtr.h"
#include "nsCoord.h"
#include "nsIFrame.h"
#include "nsISelectionListener.h"

class nsFrameSelection;
class nsIContent;

struct nsPoint;

namespace mozilla {
class PresShell;
struct FrameAndOffset;  // defined in SelectionMovementUtils.h
namespace dom {
class Element;
class Selection;
}  // namespace dom

// -----------------------------------------------------------------------------
// AccessibleCaretManager does not deal with events or callbacks directly. It
// relies on AccessibleCaretEventHub to call its public methods to do the work.
// All codes needed to interact with PresShell, Selection, and AccessibleCaret
// should be written in AccessibleCaretManager.
//
// None the public methods in AccessibleCaretManager will flush layout or style
// prior to performing its task. The caller must ensure the layout is up to
// date.
// TODO: it's unclear, whether that's true. `OnSelectionChanged` calls
// `UpdateCarets`, which may flush layout.
//
// Please see the wiki page for more information.
// https://wiki.mozilla.org/AccessibleCaret
//
class AccessibleCaretManager {
 public:
  // @param aPresShell may be nullptr for testing.
  explicit AccessibleCaretManager(PresShell* aPresShell);
  virtual ~AccessibleCaretManager() = default;

  // Called by AccessibleCaretEventHub to inform us that PresShell is destroyed.
  void Terminate();

  // The aPoint in the following public methods should be relative to root
  // frame.

  // Press caret on the given point. Return NS_OK if the point is actually on
  // one of the carets.
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult PressCaret(const nsPoint& aPoint, EventClassID aEventClass);

  // Drag caret to the given point. It's required to call PressCaret()
  // beforehand.
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult DragCaret(const nsPoint& aPoint);

  // Release caret from he previous press action. It's required to call
  // PressCaret() beforehand.
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult ReleaseCaret();

  // A quick single tap on caret on given point without dragging.
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult TapCaret(const nsPoint& aPoint);

  // Select a word or bring up paste shortcut (if Gaia is listening) under the
  // given point.
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult SelectWordOrShortcut(const nsPoint& aPoint);

  // Handle scroll-start event.
  MOZ_CAN_RUN_SCRIPT
  virtual void OnScrollStart();

  // Handle scroll-end event.
  MOZ_CAN_RUN_SCRIPT
  virtual void OnScrollEnd();

  // Handle ScrollPositionChanged from nsIScrollObserver. This might be called
  // at anytime, not necessary between OnScrollStart and OnScrollEnd.
  MOZ_CAN_RUN_SCRIPT
  virtual void OnScrollPositionChanged();

  // Handle reflow event.
  MOZ_CAN_RUN_SCRIPT
  virtual void OnReflow();

  // Handle blur event from nsFocusManager.
  MOZ_CAN_RUN_SCRIPT
  virtual void OnBlur();

  // Handle NotifySelectionChanged event from nsISelectionListener.
  // @param aReason potentially multiple of the reasons defined in
  //                nsISelectionListener.idl.
  MOZ_CAN_RUN_SCRIPT
  virtual nsresult OnSelectionChanged(dom::Document* aDoc, dom::Selection* aSel,
                                      int16_t aReason);
  // Handle key event.
  MOZ_CAN_RUN_SCRIPT
  virtual void OnKeyboardEvent();

  // Update the manager with the last input source that was observed. This
  // is used in part to determine if the carets should be shown or hidden.
  void SetLastInputSource(uint16_t aInputSource);

  // Returns True indicating that we should disable APZ to avoid jumpy carets.
  bool ShouldDisableApz() const;

 protected:
  class Carets;

  // @param aPresShell may be nullptr for testing.
  AccessibleCaretManager(PresShell* aPresShell, Carets aCarets);

  // This enum representing the number of AccessibleCarets on the screen.
  enum class CaretMode : uint8_t {
    // No caret on the screen.
    None,

    // One caret, i.e. the selection is collapsed.
    Cursor,

    // Two carets, i.e. the selection is not collapsed.
    Selection
  };

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const CaretMode& aCaretMode);

  enum class UpdateCaretsHint : uint8_t {
    // Update everything including appearance and position.
    Default,

    // Update everything while respecting the old appearance. For example, if
    // the caret in cursor mode is hidden due to blur, do not change its
    // appearance to Normal.
    RespectOldAppearance,

    // No CaretStateChangedEvent will be dispatched in the end of
    // UpdateCarets().
    DispatchNoEvent,
  };

  using UpdateCaretsHintSet = mozilla::EnumSet<UpdateCaretsHint>;

  friend std::ostream& operator<<(std::ostream& aStream,
                                  const UpdateCaretsHint& aResult);

  enum class Terminated : bool { No, Yes };

  // This method could kill the shell, so callers to methods that call
  // MaybeFlushLayout should ensure the event hub that owns us is still alive.
  //
  // See the mRefCnt assertions in AccessibleCaretEventHub.
  //
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT virtual Terminated MaybeFlushLayout();

  // Update carets based on current selection status. This function will flush
  // layout, so caller must ensure the PresShell is still valid after calling
  // this method.
  MOZ_CAN_RUN_SCRIPT
  void UpdateCarets(
      const UpdateCaretsHintSet& aHints = UpdateCaretsHint::Default);

  // Force hiding all carets regardless of the current selection status, and
  // dispatch CaretStateChangedEvent if one of the carets is logically-visible.
  MOZ_CAN_RUN_SCRIPT
  void HideCaretsAndDispatchCaretStateChangedEvent();

  MOZ_CAN_RUN_SCRIPT
  void UpdateCaretsForCursorMode(const UpdateCaretsHintSet& aHints);

  MOZ_CAN_RUN_SCRIPT
  void UpdateCaretsForSelectionMode(const UpdateCaretsHintSet& aHints);

  // Provide haptic / touch feedback, primarily for selection on longpress and
  // caret dragging.
  void ProvideHapticFeedback(mozilla::HapticFeedbackType aType);

  // Get the nearest enclosing focusable frame of aFrame.
  // @return focusable frame if there is any; nullptr otherwise.
  nsIFrame* GetFocusableFrame(nsIFrame* aFrame) const;

  // Change focus to aFrame if it isn't nullptr. Otherwise, clear the old focus
  // then re-focus the window.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void ChangeFocusToOrClearOldFocus(
      nsIFrame* aFrame) const;

  MOZ_CAN_RUN_SCRIPT
  nsresult SelectWord(nsIFrame* aFrame, const nsPoint& aPoint) const;
  MOZ_CAN_RUN_SCRIPT void SetSelectionDragState(bool aState) const;

  // Return true if the candidate string is a phone number.
  bool IsPhoneNumber(const nsAString& aCandidate) const;

  // Extend the current selection forwards and backwards if it's already a
  // phone number.
  MOZ_CAN_RUN_SCRIPT
  void SelectMoreIfPhoneNumber() const;

  // Extend the current phone number selection in the requested direction.
  MOZ_CAN_RUN_SCRIPT
  void ExtendPhoneNumberSelection(const nsAString& aDirection) const;

  void SetSelectionDirection(nsDirection aDir) const;

  /**
   * Return a frame and offset where to put the first accessible caret in the
   * selection mode. The result may be a non-selectable frame which is for a
   * child of a selectable content.  If aOutContent is set and its pointee is
   * different from the frame content of the result, it means that the result is
   * the child at aOutContent and aOutOffsetInContent.
   *
   * @param aRange      The range, typically the first range of `Selection`.
   * @param aOutContent [optional] If set, this will be set to the first
   *                    selectable container in aRange. It's typically a
   *                    container element.
   * @param aOutOffsetInContent
   *                    [optional] If set, this will be set to the offset in
   *                    the first selectable content (i.e., aOutContent).
   *                    NOTE: {*aOutContent, *aOutOffsetInContent} means that
   *                    it's the start boundary of actual selectable range at a
   *                    container element.
   * @return            mFrame is the first meaningful frame whose content is
   *                    selected by aRange. Typically, a text frame or a image
   *                    frame. Or a container frame which is not selectable but
   *                    its parent is selectable.
   *                    mOffsetInFrameContent is the offset in
   *                    mFrame->GetContent().
   *                    I.e, if mFrame->GetContent() is not a void element,
   *                    {mFrame->GetContent(), mOffsetInFrameContent} means that
   *                    it's the start boundary of visible/meaningful selection
   *                    start boundary at a leaf node like a `Text` or position
   *                    at the first non-selectable element in selectable node.
   */
  FrameAndOffset GetFirstVisibleLeafFrameOrUnselectableChildFrame(
      dom::Range& aRange, nsIContent** aOutContent = nullptr,
      int32_t* aOutOffsetInContent = nullptr) const;

  /**
   * Return a frame and offset where to put the last accessible caret in the
   * selection mode. The result may be a non-selectable frame which is for a
   * child of a selectable content.  If aOutContent is set and its pointee is
   * different from the frame content of the result, it means that the result is
   * the previous sibling of a child at aOutContent and aOutOffsetInContent.
   *
   * @param aRange      The range, typically the last range of `Selection`.
   * @param aOutContent [optional] If set, this will be set to the last
   *                    selectable container in aRange. It's typically a
   *                    container element.
   * @param aOutOffsetInContent
   *                    [optional] If set, this will be set to the offset in
   *                    the last selectable container (i.e., aOutContent).
   *                    NOTE: {*aOutContent, *aOutOffsetInContent} means that
   *                    it's the end boundary of actual selectable range at a
   *                    container element.
   * @return            mFrame is the last meaningful frame whose content is
   *                    selected by aRange. Typically, a text frame or a image
   *                    frame. Or a container frame which is not selectable but
   *                    its parent is selectable.
   *                    mOffsetInFrameContent is the offset in
   *                    mFrame->GetContent().
   *                    I.e, if mFrame->GetContent() is not a void element,
   *                    {mFrame->GetContent(), mOffsetInFrameContent} means that
   *                    it's the end boundary of visible/meaningful selection
   *                    end boundary at a leaf node like a `Text` or position at
   *                    the last non-selectable element in selectable node.
   */
  FrameAndOffset GetLastVisibleLeafFrameOrUnselectableChildFrame(
      dom::Range& aRange, nsIContent** aOutContent = nullptr,
      int32_t* aOutOffsetInContent = nullptr) const;

  MOZ_CAN_RUN_SCRIPT nsresult DragCaretInternal(const nsPoint& aPoint);
  nsPoint AdjustDragBoundary(const nsPoint& aPoint) const;

  // Start the selection scroll timer if the caret is being dragged out of
  // the scroll port.
  MOZ_CAN_RUN_SCRIPT
  void StartSelectionAutoScrollTimer(const nsPoint& aPoint) const;
  void StopSelectionAutoScrollTimer() const;

  void ClearMaintainedSelection() const;

  static dom::Element* GetEditingHostForFrame(const nsIFrame* aFrame);
  dom::Selection* GetSelection() const;
  already_AddRefed<nsFrameSelection> GetFrameSelection() const;

  MOZ_CAN_RUN_SCRIPT
  nsAutoString StringifiedSelection() const;

  // Get the union of all the child frame scrollable overflow rects for aFrame,
  // which is used as a helper function to restrict the area where the caret can
  // be dragged. Returns the rect relative to aFrame.
  static nsRect GetAllChildFrameRectsUnion(nsIFrame* aFrame);

  // Restrict the active caret's dragging position. If the active caret is
  // dragged across the inactive caret, the two carets swap roles, i.e.
  // mActiveCaret becomes the other caret.
  //
  // @param aOffsets is the new position of the active caret. If it is the same
  // as the inactive caret's position, it is moved one character away from the
  // inactive caret so that the selection won't be collapsed.
  // @return true if the aOffsets is suitable for changing the selection.
  bool RestrictCaretDraggingOffsets(nsIFrame::ContentOffsets& aOffsets);

  // ---------------------------------------------------------------------------
  // The following functions are made virtual for stubbing or mocking in gtest.
  //
  // @return Yes if Terminate() had been called.
  virtual Terminated IsTerminated() const {
    return mPresShell ? Terminated::No : Terminated::Yes;
  }

  // Get caret mode based on current selection.
  virtual CaretMode GetCaretMode() const;

  // @return true if aStartFrame comes before aEndFrame.
  virtual bool CompareTreePosition(const nsIFrame* aStartFrame,
                                   int32_t aStartOffset,
                                   const nsIFrame* aEndFrame,
                                   int32_t aEndOffset) const;

  // Check if the two carets is overlapping to become tilt.
  // @return true if the two carets become tilt; false, otherwise.
  virtual bool UpdateCaretsForOverlappingTilt();

  // Make the two carets always tilt.
  virtual void UpdateCaretsForAlwaysTilt(const nsIFrame* aStartFrame,
                                         const nsIFrame* aEndFrame);

  // Check whether AccessibleCaret is displayable in cursor mode or not.
  // @param aOutFrame returns frame of the cursor if it's displayable.
  // @param aOutOffset returns frame offset as well.
  virtual bool IsCaretDisplayableInCursorMode(
      nsIFrame** aOutFrame = nullptr, int32_t* aOutOffset = nullptr) const;

  virtual bool HasNonEmptyTextContent(nsINode* aNode) const;

  // This function will flush layout, so caller must ensure the PresShell is
  // still valid after calling this method.
  // @param aPoint The event point when the user is pressing or dragging a
  //               caret, which is relative to the root frame.
  MOZ_CAN_RUN_SCRIPT
  virtual void DispatchCaretStateChangedEvent(dom::CaretChangedReason aReason,
                                              const nsPoint* aPoint = nullptr);

  // ---------------------------------------------------------------------------
  // Member variables
  //
  nscoord mOffsetYToCaretLogicalPosition = NS_UNCONSTRAINEDSIZE;

  // AccessibleCaretEventHub owns us by a UniquePtr. When it's destroyed, we'll
  // also be destroyed. No need to worry if we outlive mPresShell.
  //
  // mPresShell will be set to nullptr in Terminate(). Therefore mPresShell is
  // nullptr either we are in gtest or PresShell::IsDestroying() is true.
  PresShell* MOZ_NON_OWNING_REF mPresShell = nullptr;

  class Carets {
   public:
    Carets(UniquePtr<AccessibleCaret> aFirst,
           UniquePtr<AccessibleCaret> aSecond);

    Carets(Carets&&) = default;
    Carets(const Carets&) = delete;
    Carets& operator=(const Carets&) = delete;

    AccessibleCaret* GetFirst() const { return mFirst.get(); }

    AccessibleCaret* GetSecond() const { return mSecond.get(); }

    bool HasLogicallyVisibleCaret() const {
      return mFirst->IsLogicallyVisible() || mSecond->IsLogicallyVisible();
    }

    bool HasVisuallyVisibleCaret() const {
      return mFirst->IsVisuallyVisible() || mSecond->IsVisuallyVisible();
    }

    void Terminate() {
      mFirst = nullptr;
      mSecond = nullptr;
    }

   private:
    // First caret is attached to nsCaret in cursor mode, and is attached to
    // selection highlight as the left caret in selection mode.
    UniquePtr<AccessibleCaret> mFirst;

    // Second caret is used solely in selection mode, and is attached to
    // selection highlight as the right caret.
    UniquePtr<AccessibleCaret> mSecond;
  };

  Carets mCarets;

  // The caret being pressed or dragged.
  AccessibleCaret* mActiveCaret = nullptr;

  // The caret mode since last update carets.
  CaretMode mLastUpdateCaretMode = CaretMode::None;

  // The last input source that the event hub saw. We use this to decide whether
  // or not show the carets when the selection is updated, as we want to hide
  // the carets for mouse-triggered selection changes but show them for other
  // input types such as touch.
  uint16_t mLastInputSource = dom::MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;

  // Set to true in OnScrollStart() and set to false in OnScrollEnd().
  bool mIsScrollStarted = false;

  class LayoutFlusher final {
   public:
    LayoutFlusher() = default;

    ~LayoutFlusher();

    LayoutFlusher(const LayoutFlusher&) = delete;
    LayoutFlusher& operator=(const LayoutFlusher&) = delete;

    MOZ_CAN_RUN_SCRIPT void MaybeFlush(const PresShell& aPresShell);

    // Set to false to disallow flushing layout in some callbacks such as
    // OnReflow(), OnScrollStart(), OnScrollStart(), or
    // OnScrollPositionChanged().
    bool mAllowFlushing = true;

   private:
    // Whether we're flushing layout, used for sanity-checking.
    bool mFlushing = false;
  };

  LayoutFlusher mLayoutFlusher;

  // Set to True if one of the caret's position is changed in last update.
  bool mIsCaretPositionChanged = false;

  class DesiredAsyncPanZoomState final {
   public:
    void Update(const AccessibleCaretManager& aAccessibleCaretManager);

    bool ShouldDisable() const { return mValue == Value::Disabled; }

   private:
    enum class Value : bool { Disabled, Enabled };

    Value mValue = Value::Enabled;
  };

  DesiredAsyncPanZoomState mDesiredAsyncPanZoomState;

  static const int32_t kAutoScrollTimerDelay = 30;

  // Clicking on the boundary of input or textarea will move the caret to the
  // front or end of the content. To avoid this, we need to deflate the content
  // boundary by 61 app units, which is 1 pixel + 1 app unit as defined in
  // AppUnit.h.
  static const int32_t kBoundaryAppUnits = 61;

  enum ScriptUpdateMode : int32_t {
    // By default, always hide carets for selection changes due to JS calls.
    kScriptAlwaysHide,
    // Update any visible carets for selection changes due to JS calls,
    // but don't show carets if carets are hidden.
    kScriptUpdateVisible,
    // Always show carets for selection changes due to JS calls.
    kScriptAlwaysShow
  };
};

std::ostream& operator<<(std::ostream& aStream,
                         const AccessibleCaretManager::CaretMode& aCaretMode);

std::ostream& operator<<(
    std::ostream& aStream,
    const AccessibleCaretManager::UpdateCaretsHint& aResult);

}  // namespace mozilla

#endif  // AccessibleCaretManager_h
