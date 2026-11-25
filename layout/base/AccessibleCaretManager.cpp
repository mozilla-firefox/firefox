/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AccessibleCaretManager.h"

#include <utility>

#include "AccessibleCaret.h"
#include "AccessibleCaretEventHub.h"
#include "AccessibleCaretLogger.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/NodeFilterBinding.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/TreeWalker.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StaticPrefs_layout.h"
#include "nsCaret.h"
#include "nsContainerFrame.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsFocusManager.h"
#include "nsIFrame.h"
#include "nsFrameSelection.h"
#include "nsGenericHTMLElement.h"
#include "nsIHapticFeedback.h"
#include "nsIScrollableFrame.h"
#include "nsLayoutUtils.h"
#include "nsServiceManagerUtils.h"

namespace mozilla {

#undef AC_LOG
#define AC_LOG(message, ...) \
  AC_LOG_BASE("AccessibleCaretManager (%p): " message, this, ##__VA_ARGS__);

#undef AC_LOGV
#define AC_LOGV(message, ...) \
  AC_LOGV_BASE("AccessibleCaretManager (%p): " message, this, ##__VA_ARGS__);

using namespace dom;
using Appearance = AccessibleCaret::Appearance;
using PositionChangedResult = AccessibleCaret::PositionChangedResult;

#define AC_PROCESS_ENUM_TO_STREAM(e) \
  case (e):                          \
    aStream << #e;                   \
    break;
std::ostream& operator<<(std::ostream& aStream,
                         const AccessibleCaretManager::CaretMode& aCaretMode) {
  using CaretMode = AccessibleCaretManager::CaretMode;
  switch (aCaretMode) {
    AC_PROCESS_ENUM_TO_STREAM(CaretMode::None);
    AC_PROCESS_ENUM_TO_STREAM(CaretMode::Cursor);
    AC_PROCESS_ENUM_TO_STREAM(CaretMode::Selection);
  }
  return aStream;
}

std::ostream& operator<<(
    std::ostream& aStream,
    const AccessibleCaretManager::UpdateCaretsHint& aHint) {
  using UpdateCaretsHint = AccessibleCaretManager::UpdateCaretsHint;
  switch (aHint) {
    AC_PROCESS_ENUM_TO_STREAM(UpdateCaretsHint::Default);
    AC_PROCESS_ENUM_TO_STREAM(UpdateCaretsHint::RespectOldAppearance);
    AC_PROCESS_ENUM_TO_STREAM(UpdateCaretsHint::DispatchNoEvent);
  }
  return aStream;
}
#undef AC_PROCESS_ENUM_TO_STREAM

AccessibleCaretManager::AccessibleCaretManager(PresShell* aPresShell)
    : AccessibleCaretManager{
          aPresShell,
          Carets{aPresShell ? MakeUnique<AccessibleCaret>(aPresShell) : nullptr,
                 aPresShell ? MakeUnique<AccessibleCaret>(aPresShell)
                            : nullptr}} {}

AccessibleCaretManager::AccessibleCaretManager(PresShell* aPresShell,
                                               Carets aCarets)
    : mPresShell{aPresShell}, mCarets{std::move(aCarets)} {}

AccessibleCaretManager::LayoutFlusher::~LayoutFlusher() {
  MOZ_RELEASE_ASSERT(!mFlushing, "Going away in MaybeFlush? Bad!");
}

void AccessibleCaretManager::Terminate() {
  mCarets.Terminate();
  mActiveCaret = nullptr;
  mPresShell = nullptr;
}

nsresult AccessibleCaretManager::OnSelectionChanged(Document* aDoc,
                                                    Selection* aSel,
                                                    int16_t aReason) {
  Selection* selection = GetSelection();
  AC_LOG("%s: aSel: %p, GetSelection(): %p, aReason: %d", __FUNCTION__, aSel,
         selection, aReason);
  if (aSel != selection) {
    return NS_OK;
  }

  // eSetSelection events from the Fennec widget IME can be generated
  // by autoSuggest / autoCorrect composition changes, or by TYPE_REPLACE_TEXT
  // actions, either positioning cursor for text insert, or selecting
  // text-to-be-replaced. None should affect AccessibleCaret visibility.
  if (aReason & nsISelectionListener::IME_REASON) {
    return NS_OK;
  }

  // Move the cursor by JavaScript or unknown internal call.
  if (aReason == nsISelectionListener::NO_REASON ||
      aReason == nsISelectionListener::JS_REASON) {
    auto mode = static_cast<ScriptUpdateMode>(
        StaticPrefs::layout_accessiblecaret_script_change_update_mode());
    if (mode == kScriptAlwaysShow ||
        (mode == kScriptUpdateVisible && mCarets.HasLogicallyVisibleCaret())) {
      UpdateCarets();
      return NS_OK;
    }
    // Default for NO_REASON is to make hidden.
    HideCaretsAndDispatchCaretStateChangedEvent();
    return NS_OK;
  }

  // Move cursor by keyboard.
  if (aReason & nsISelectionListener::KEYPRESS_REASON) {
    HideCaretsAndDispatchCaretStateChangedEvent();
    return NS_OK;
  }

  // OnBlur() might be called between mouse down and mouse up, so we hide carets
  // upon mouse down anyway, and update carets upon mouse up.
  if (aReason & nsISelectionListener::MOUSEDOWN_REASON) {
    HideCaretsAndDispatchCaretStateChangedEvent();
    return NS_OK;
  }

  // Range will collapse after cutting or copying text.
  if (aReason & (nsISelectionListener::COLLAPSETOSTART_REASON |
                 nsISelectionListener::COLLAPSETOEND_REASON)) {
    HideCaretsAndDispatchCaretStateChangedEvent();
    return NS_OK;
  }

  // For mouse input we don't want to show the carets.
  if (StaticPrefs::layout_accessiblecaret_hide_carets_for_mouse_input() &&
      mLastInputSource == MouseEvent_Binding::MOZ_SOURCE_MOUSE) {
    HideCaretsAndDispatchCaretStateChangedEvent();
    return NS_OK;
  }

  // When we want to hide the carets for mouse input, hide them for select
  // all action fired by keyboard as well.
  if (StaticPrefs::layout_accessiblecaret_hide_carets_for_mouse_input() &&
      mLastInputSource == MouseEvent_Binding::MOZ_SOURCE_KEYBOARD &&
      (aReason & nsISelectionListener::SELECTALL_REASON)) {
    HideCaretsAndDispatchCaretStateChangedEvent();
    return NS_OK;
  }

  UpdateCarets();
  return NS_OK;
}

void AccessibleCaretManager::HideCaretsAndDispatchCaretStateChangedEvent() {
  if (!mCarets.HasLogicallyVisibleCaret()) {
    return;
  }

  {
    // Mutating the caret elements may run scripts that destroy the PresShell
    // and the carets. Defer them until we're done with both carets.
    nsAutoScriptBlocker scriptBlocker;
    AC_LOG("%s", __FUNCTION__);
    mCarets.GetFirst()->SetAppearance(Appearance::None);
    mCarets.GetSecond()->SetAppearance(Appearance::None);
    mIsCaretPositionChanged = false;
  }

  DispatchCaretStateChangedEvent(CaretChangedReason::Visibilitychange);
}

auto AccessibleCaretManager::MaybeFlushLayout() -> Terminated {
  if (mPresShell) {
    // `MaybeFlush` doesn't access the PresShell after flushing, so it's OK to
    // mark it as live.
    mLayoutFlusher.MaybeFlush(MOZ_KnownLive(*mPresShell));
  }

  return IsTerminated();
}

void AccessibleCaretManager::UpdateCarets(const UpdateCaretsHintSet& aHint) {
  if (MaybeFlushLayout() == Terminated::Yes) {
    return;
  }

  mLastUpdateCaretMode = GetCaretMode();

  switch (mLastUpdateCaretMode) {
    case CaretMode::None:
      HideCaretsAndDispatchCaretStateChangedEvent();
      break;
    case CaretMode::Cursor:
      UpdateCaretsForCursorMode(aHint);
      break;
    case CaretMode::Selection:
      UpdateCaretsForSelectionMode(aHint);
      break;
  }

  if (IsTerminated() == Terminated::Yes) {
    return;
  }

  mDesiredAsyncPanZoomState.Update(*this);
}

bool AccessibleCaretManager::IsCaretDisplayableInCursorMode(
    nsIFrame** aOutFrame, int32_t* aOutOffset) const {
  RefPtr<nsCaret> caret = mPresShell->GetCaret();
  if (!caret || !caret->IsVisible()) {
    return false;
  }

  int32_t offset = 0;
  nsIFrame* frame =
      nsCaret::GetFrameAndOffset(GetSelection(), nullptr, 0, &offset);

  if (!frame) {
    return false;
  }

  if (!GetEditingHostForFrame(frame)) {
    return false;
  }

  if (aOutFrame) {
    *aOutFrame = frame;
  }

  if (aOutOffset) {
    *aOutOffset = offset;
  }

  return true;
}

bool AccessibleCaretManager::HasNonEmptyTextContent(nsINode* aNode) const {
  return nsContentUtils::HasNonEmptyTextContent(
      aNode, nsContentUtils::eRecurseIntoChildren);
}

void AccessibleCaretManager::UpdateCaretsForCursorMode(
    const UpdateCaretsHintSet& aHints) {
  AC_LOG("%s, selection: %p", __FUNCTION__, GetSelection());

  int32_t offset = 0;
  nsIFrame* frame = nullptr;
  if (!IsCaretDisplayableInCursorMode(&frame, &offset)) {
    HideCaretsAndDispatchCaretStateChangedEvent();
    return;
  }

  auto result = PositionChangedResult::NotChanged;
  {
    // Mutating the caret elements may run scripts that destroy the PresShell
    // and the carets. Defer them until we're done with both carets.
    nsAutoScriptBlocker scriptBlocker;
    result = mCarets.GetFirst()->SetPosition(frame, offset);

    switch (result) {
      case PositionChangedResult::NotChanged:
      case PositionChangedResult::Position:
      case PositionChangedResult::Zoom:
        if (!aHints.contains(UpdateCaretsHint::RespectOldAppearance)) {
          if (HasNonEmptyTextContent(GetEditingHostForFrame(frame))) {
            mCarets.GetFirst()->SetAppearance(Appearance::Normal);
          } else if (
              StaticPrefs::
                  layout_accessiblecaret_caret_shown_when_long_tapping_on_empty_content()) {
            if (mCarets.GetFirst()->IsLogicallyVisible()) {
              // Possible cases are: 1) SelectWordOrShortcut() sets the
              // appearance to Normal. 2) When the caret is out of viewport and
              // now scrolling into viewport, it has appearance NormalNotShown.
              mCarets.GetFirst()->SetAppearance(Appearance::Normal);
            } else {
              // Possible cases are: a) Single tap on current empty content;
              // OnSelectionChanged() sets the appearance to None due to
              // MOUSEDOWN_REASON. b) Single tap on other empty content;
              // OnBlur() sets the appearance to None.
              //
              // Do nothing to make the appearance remains None so that it can
              // be distinguished from case 2). Also do not set the appearance
              // to NormalNotShown here like the default update behavior.
            }
          } else {
            mCarets.GetFirst()->SetAppearance(Appearance::NormalNotShown);
          }
        }
        break;

      case PositionChangedResult::Invisible:
        mCarets.GetFirst()->SetAppearance(Appearance::NormalNotShown);
        break;
    }

    mCarets.GetSecond()->SetAppearance(Appearance::None);
  }

  if (IsTerminated() == Terminated::Yes) {
    return;
  }

  mIsCaretPositionChanged = (result == PositionChangedResult::Position);

  if (!aHints.contains(UpdateCaretsHint::DispatchNoEvent) && !mActiveCaret) {
    DispatchCaretStateChangedEvent(CaretChangedReason::Updateposition);
  }
}

void AccessibleCaretManager::UpdateCaretsForSelectionMode(
    const UpdateCaretsHintSet& aHints) {
  AC_LOG("%s: selection: %p", __FUNCTION__, GetSelection());

  int32_t startOffset = 0;
  nsIFrame* startFrame =
      GetFrameForFirstRangeStartOrLastRangeEnd(eDirNext, &startOffset);

  int32_t endOffset = 0;
  nsIFrame* endFrame =
      GetFrameForFirstRangeStartOrLastRangeEnd(eDirPrevious, &endOffset);

  AutoWeakFrame weakStartFrame = startFrame;
  AutoWeakFrame weakEndFrame = endFrame;

  if (!CompareTreePosition(startFrame, endFrame)) {
    // XXX: Do we really have to hide carets if this condition isn't satisfied?
    HideCaretsAndDispatchCaretStateChangedEvent();
    return;
  }

  auto updateSingleCaret = [aHints](AccessibleCaret* aCaret, nsIFrame* aFrame,
                                    int32_t aOffset) -> PositionChangedResult {
    PositionChangedResult result = aCaret->SetPosition(aFrame, aOffset);

    switch (result) {
      case PositionChangedResult::NotChanged:
      case PositionChangedResult::Position:
      case PositionChangedResult::Zoom:
        if (!aHints.contains(UpdateCaretsHint::RespectOldAppearance)) {
          aCaret->SetAppearance(Appearance::Normal);
        }
        break;

      case PositionChangedResult::Invisible:
        aCaret->SetAppearance(Appearance::NormalNotShown);
        break;
    }
    return result;
  };

  auto firstCaretResult = PositionChangedResult::NotChanged;
  auto secondCaretResult = PositionChangedResult::NotChanged;
  {
    // Mutating the caret elements may run scripts that destroy the PresShell
    // and the carets. Defer them until we're done with both carets.
    nsAutoScriptBlocker scriptBlocker;
    firstCaretResult =
        updateSingleCaret(mCarets.GetFirst(), startFrame, startOffset);
    secondCaretResult =
        updateSingleCaret(mCarets.GetSecond(), endFrame, endOffset);
  }

  if (IsTerminated() == Terminated::Yes) {
    return;
  }

  mIsCaretPositionChanged =
      firstCaretResult == PositionChangedResult::Position ||
      secondCaretResult == PositionChangedResult::Position;

  if (mIsCaretPositionChanged) {
    // Flush layout to make the carets intersection correct.
    if (MaybeFlushLayout() == Terminated::Yes) {
      return;
    }
  }

  // Bail out and hide the carets if either the start frame or end frame is
  // destroyed.
  if ((startFrame && !weakStartFrame.IsAlive()) ||
      (endFrame && !weakEndFrame.IsAlive())) {
    HideCaretsAndDispatchCaretStateChangedEvent();
    return;
  }

  if (!aHints.contains(UpdateCaretsHint::RespectOldAppearance)) {
    nsAutoScriptBlocker scriptBlocker;

    // Only check for tilt carets when the caller doesn't ask us to preserve
    // old appearance. Otherwise we might override the appearance set by the
    // caller.
    if (StaticPrefs::layout_accessiblecaret_always_tilt()) {
      UpdateCaretsForAlwaysTilt(startFrame, endFrame);
    } else {
      UpdateCaretsForOverlappingTilt();
    }
  }

  if (!aHints.contains(UpdateCaretsHint::DispatchNoEvent) && !mActiveCaret) {
    DispatchCaretStateChangedEvent(CaretChangedReason::Updateposition);
  }
}

void AccessibleCaretManager::DesiredAsyncPanZoomState::Update(
    const AccessibleCaretManager& aAccessibleCaretManager) {
  if (aAccessibleCaretManager.mActiveCaret) {
    // No need to disable APZ when dragging the caret.
    mValue = Value::Enabled;
    return;
  }

  if (aAccessibleCaretManager.mIsScrollStarted) {
    // During scrolling, the caret's position is changed only if it is in a
    // position:fixed or a "stuck" position:sticky frame subtree.
    mValue = aAccessibleCaretManager.mIsCaretPositionChanged ? Value::Disabled
                                                             : Value::Enabled;
    return;
  }

  // For other cases, we can only reliably detect whether the caret is in a
  // position:fixed frame subtree.
  switch (aAccessibleCaretManager.mLastUpdateCaretMode) {
    case CaretMode::None:
      mValue = Value::Enabled;
      break;
    case CaretMode::Cursor:
      mValue =
          (aAccessibleCaretManager.mCarets.GetFirst()->IsVisuallyVisible() &&
           aAccessibleCaretManager.mCarets.GetFirst()
               ->IsInPositionFixedSubtree())
              ? Value::Disabled
              : Value::Enabled;
      break;
    case CaretMode::Selection:
      mValue =
          ((aAccessibleCaretManager.mCarets.GetFirst()->IsVisuallyVisible() &&
            aAccessibleCaretManager.mCarets.GetFirst()
                ->IsInPositionFixedSubtree()) ||
           (aAccessibleCaretManager.mCarets.GetSecond()->IsVisuallyVisible() &&
            aAccessibleCaretManager.mCarets.GetSecond()
                ->IsInPositionFixedSubtree()))
              ? Value::Disabled
              : Value::Enabled;
      break;
  }
}

bool AccessibleCaretManager::UpdateCaretsForOverlappingTilt() {
  if (!mCarets.GetFirst()->IsVisuallyVisible() ||
      !mCarets.GetSecond()->IsVisuallyVisible()) {
    return false;
  }

  if (!mCarets.GetFirst()->Intersects(*mCarets.GetSecond())) {
    mCarets.GetFirst()->SetAppearance(Appearance::Normal);
    mCarets.GetSecond()->SetAppearance(Appearance::Normal);
    return false;
  }

  if (mCarets.GetFirst()->LogicalPosition().x <=
      mCarets.GetSecond()->LogicalPosition().x) {
    mCarets.GetFirst()->SetAppearance(Appearance::Left);
    mCarets.GetSecond()->SetAppearance(Appearance::Right);
  } else {
    mCarets.GetFirst()->SetAppearance(Appearance::Right);
    mCarets.GetSecond()->SetAppearance(Appearance::Left);
  }

  return true;
}

void AccessibleCaretManager::UpdateCaretsForAlwaysTilt(
    const nsIFrame* aStartFrame, const nsIFrame* aEndFrame) {
  // When a short LTR word in RTL environment is selected, the two carets
  // tilted inward might be overlapped. Make them tilt outward.
  if (UpdateCaretsForOverlappingTilt()) {
    return;
  }

  if (mCarets.GetFirst()->IsVisuallyVisible()) {
    auto startFrameWritingMode = aStartFrame->GetWritingMode();
    mCarets.GetFirst()->SetAppearance(startFrameWritingMode.IsBidiLTR()
                                          ? Appearance::Left
                                          : Appearance::Right);
  }
  if (mCarets.GetSecond()->IsVisuallyVisible()) {
    auto endFrameWritingMode = aEndFrame->GetWritingMode();
    mCarets.GetSecond()->SetAppearance(
        endFrameWritingMode.IsBidiLTR() ? Appearance::Right : Appearance::Left);
  }
}

void AccessibleCaretManager::ProvideHapticFeedback() {
  if (StaticPrefs::layout_accessiblecaret_hapticfeedback()) {
    if (nsCOMPtr<nsIHapticFeedback> haptic =
            do_GetService("@mozilla.org/widget/hapticfeedback;1")) {
      haptic->PerformSimpleAction(haptic->LongPress);
    }
  }
}

nsresult AccessibleCaretManager::PressCaret(const nsPoint& aPoint,
                                            EventClassID aEventClass) {
  nsresult rv = NS_ERROR_FAILURE;

  MOZ_ASSERT(aEventClass == eMouseEventClass || aEventClass == eTouchEventClass,
             "Unexpected event class!");

  using TouchArea = AccessibleCaret::TouchArea;
  TouchArea touchArea =
      aEventClass == eMouseEventClass ? TouchArea::CaretImage : TouchArea::Full;

  if (mCarets.GetFirst()->Contains(aPoint, touchArea)) {
    mActiveCaret = mCarets.GetFirst();
    SetSelectionDirection(eDirPrevious);
  } else if (mCarets.GetSecond()->Contains(aPoint, touchArea)) {
    mActiveCaret = mCarets.GetSecond();
    SetSelectionDirection(eDirNext);
  }

  if (mActiveCaret) {
    mOffsetYToCaretLogicalPosition =
        mActiveCaret->LogicalPosition().y - aPoint.y;
    SetSelectionDragState(true);
    DispatchCaretStateChangedEvent(CaretChangedReason::Presscaret, &aPoint);
    rv = NS_OK;
  }

  return rv;
}

nsresult AccessibleCaretManager::DragCaret(const nsPoint& aPoint) {
  MOZ_ASSERT(mActiveCaret);
  MOZ_ASSERT(GetCaretMode() != CaretMode::None);

  if (!mPresShell || !mPresShell->GetRootFrame() || !GetSelection()) {
    return NS_ERROR_NULL_POINTER;
  }

  StopSelectionAutoScrollTimer();
  DragCaretInternal(aPoint);

  // We want to scroll the page even if we failed to drag the caret.
  StartSelectionAutoScrollTimer(aPoint);
  UpdateCarets();

  if (StaticPrefs::layout_accessiblecaret_magnifier_enabled()) {
    DispatchCaretStateChangedEvent(CaretChangedReason::Dragcaret, &aPoint);
  }
  return NS_OK;
}

nsresult AccessibleCaretManager::ReleaseCaret() {
  MOZ_ASSERT(mActiveCaret);

  mActiveCaret = nullptr;
  SetSelectionDragState(false);
  mDesiredAsyncPanZoomState.Update(*this);
  DispatchCaretStateChangedEvent(CaretChangedReason::Releasecaret);
  return NS_OK;
}

nsresult AccessibleCaretManager::TapCaret(const nsPoint& aPoint) {
  MOZ_ASSERT(GetCaretMode() != CaretMode::None);

  nsresult rv = NS_ERROR_FAILURE;

  if (GetCaretMode() == CaretMode::Cursor) {
    DispatchCaretStateChangedEvent(CaretChangedReason::Taponcaret, &aPoint);
    rv = NS_OK;
  }

  return rv;
}

static EnumSet<nsLayoutUtils::FrameForPointOption> GetHitTestOptions() {
  EnumSet<nsLayoutUtils::FrameForPointOption> options = {
      nsLayoutUtils::FrameForPointOption::IgnorePaintSuppression,
      nsLayoutUtils::FrameForPointOption::IgnoreCrossDoc};
  return options;
}

nsresult AccessibleCaretManager::SelectWordOrShortcut(const nsPoint& aPoint) {
  // If the long-tap is landing on a pre-existing selection, don't replace
  // it with a new one. Instead just return and let the context menu pop up
  // on the pre-existing selection.
  if (GetCaretMode() == CaretMode::Selection &&
      GetSelection()->ContainsPoint(aPoint)) {
    AC_LOG("%s: UpdateCarets() for current selection", __FUNCTION__);
    UpdateCarets();
    ProvideHapticFeedback();
    return NS_OK;
  }

  if (!mPresShell) {
    return NS_ERROR_UNEXPECTED;
  }

  nsIFrame* rootFrame = mPresShell->GetRootFrame();
  if (!rootFrame) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Find the frame under point.
  AutoWeakFrame ptFrame = nsLayoutUtils::GetFrameForPoint(
      RelativeTo{rootFrame}, aPoint, GetHitTestOptions());
  if (!ptFrame.GetFrame()) {
    return NS_ERROR_FAILURE;
  }

  nsIFrame* focusableFrame = GetFocusableFrame(ptFrame);

#ifdef DEBUG_FRAME_DUMP
  AC_LOG("%s: Found %s under (%d, %d)", __FUNCTION__, ptFrame->ListTag().get(),
         aPoint.x, aPoint.y);
  AC_LOG("%s: Found %s focusable", __FUNCTION__,
         focusableFrame ? focusableFrame->ListTag().get() : "no frame");
#endif

  // Get ptInFrame here so that we don't need to check whether rootFrame is
  // alive later. Note that if ptFrame is being moved by
  // IMEStateManager::NotifyIME() or ChangeFocusToOrClearOldFocus() below,
  // something under the original point will be selected, which may not be the
  // original text the user wants to select.
  nsPoint ptInFrame = aPoint;
  nsLayoutUtils::TransformPoint(RelativeTo{rootFrame}, RelativeTo{ptFrame},
                                ptInFrame);

  // Firstly check long press on an empty editable content.
  Element* newFocusEditingHost = GetEditingHostForFrame(ptFrame);
  if (focusableFrame && newFocusEditingHost &&
      !HasNonEmptyTextContent(newFocusEditingHost)) {
    ChangeFocusToOrClearOldFocus(focusableFrame);

    if (StaticPrefs::
            layout_accessiblecaret_caret_shown_when_long_tapping_on_empty_content()) {
      mCarets.GetFirst()->SetAppearance(Appearance::Normal);
    }
    // We need to update carets to get correct information before dispatching
    // CaretStateChangedEvent.
    UpdateCarets();
    ProvideHapticFeedback();
    DispatchCaretStateChangedEvent(CaretChangedReason::Longpressonemptycontent);
    return NS_OK;
  }

  bool selectable = ptFrame->IsSelectable(nullptr);

#ifdef DEBUG_FRAME_DUMP
  AC_LOG("%s: %s %s selectable.", __FUNCTION__, ptFrame->ListTag().get(),
         selectable ? "is" : "is NOT");
#endif

  if (!selectable) {
    return NS_ERROR_FAILURE;
  }

  // Commit the composition string of the old editable focus element (if there
  // is any) before changing the focus.
  IMEStateManager::NotifyIME(widget::REQUEST_TO_COMMIT_COMPOSITION,
                             mPresShell->GetPresContext());
  if (!ptFrame.IsAlive()) {
    // Cannot continue because ptFrame died.
    return NS_ERROR_FAILURE;
  }

  // ptFrame is selectable. Now change the focus.
  ChangeFocusToOrClearOldFocus(focusableFrame);
  if (!ptFrame.IsAlive()) {
    // Cannot continue because ptFrame died.
    return NS_ERROR_FAILURE;
  }

  // If long tap point isn't selectable frame for caret and frame selection
  // can find a better frame for caret, we don't select a word.
  // See https://webcompat.com/issues/15953
  nsIFrame::ContentOffsets offsets =
      ptFrame->GetContentOffsetsFromPoint(ptInFrame, nsIFrame::SKIP_HIDDEN);
  if (offsets.content) {
    RefPtr<nsFrameSelection> frameSelection = GetFrameSelection();
    if (frameSelection) {
      int32_t offset;
      nsIFrame* theFrame = nsFrameSelection::GetFrameForNodeOffset(
          offsets.content, offsets.offset, offsets.associate, &offset);
      if (theFrame && theFrame != ptFrame) {
        SetSelectionDragState(true);
        frameSelection->HandleClick(
            MOZ_KnownLive(offsets.content) /* bug 1636889 */,
            offsets.StartOffset(), offsets.EndOffset(),
            nsFrameSelection::FocusMode::kCollapseToNewPoint,
            offsets.associate);
        SetSelectionDragState(false);
        ClearMaintainedSelection();

        if (StaticPrefs::
                layout_accessiblecaret_caret_shown_when_long_tapping_on_empty_content()) {
          mCarets.GetFirst()->SetAppearance(Appearance::Normal);
        }

        UpdateCarets();
        ProvideHapticFeedback();
        DispatchCaretStateChangedEvent(
            CaretChangedReason::Longpressonemptycontent);

        return NS_OK;
      }
    }
  }

  // Then try select a word under point.
  nsresult rv = SelectWord(ptFrame, ptInFrame);
  UpdateCarets();
  ProvideHapticFeedback();

  return rv;
}

void AccessibleCaretManager::OnScrollStart() {
  AC_LOG("%s", __FUNCTION__);

  nsAutoScriptBlocker scriptBlocker;
  AutoRestore<bool> saveAllowFlushingLayout(mLayoutFlusher.mAllowFlushing);
  mLayoutFlusher.mAllowFlushing = false;

  Maybe<PresShell::AutoAssertNoFlush> assert;
  if (mPresShell) {
    assert.emplace(*mPresShell);
  }

  mIsScrollStarted = true;

  if (mCarets.HasLogicallyVisibleCaret()) {
    // Dispatch the event only if one of the carets is logically visible like in
    // HideCaretsAndDispatchCaretStateChangedEvent().
    DispatchCaretStateChangedEvent(CaretChangedReason::Scroll);
  }
}

void AccessibleCaretManager::OnScrollEnd() {
  nsAutoScriptBlocker scriptBlocker;
  AutoRestore<bool> saveAllowFlushingLayout(mLayoutFlusher.mAllowFlushing);
  mLayoutFlusher.mAllowFlushing = false;

  Maybe<PresShell::AutoAssertNoFlush> assert;
  if (mPresShell) {
    assert.emplace(*mPresShell);
  }

  mIsScrollStarted = false;

  if (GetCaretMode() == CaretMode::Cursor) {
    if (!mCarets.GetFirst()->IsLogicallyVisible()) {
      // If the caret is hidden (Appearance::None) due to blur, no
      // need to update it.
      return;
    }
  }

  // For mouse input we don't want to show the carets.
  if (StaticPrefs::layout_accessiblecaret_hide_carets_for_mouse_input() &&
      mLastInputSource == MouseEvent_Binding::MOZ_SOURCE_MOUSE) {
    AC_LOG("%s: HideCaretsAndDispatchCaretStateChangedEvent()", __FUNCTION__);
    HideCaretsAndDispatchCaretStateChangedEvent();
    return;
  }

  AC_LOG("%s: UpdateCarets()", __FUNCTION__);
  UpdateCarets();
}

void AccessibleCaretManager::OnScrollPositionChanged() {
  nsAutoScriptBlocker scriptBlocker;
  AutoRestore<bool> saveAllowFlushingLayout(mLayoutFlusher.mAllowFlushing);
  mLayoutFlusher.mAllowFlushing = false;

  Maybe<PresShell::AutoAssertNoFlush> assert;
  if (mPresShell) {
    assert.emplace(*mPresShell);
  }

  if (mCarets.HasLogicallyVisibleCaret()) {
    if (mIsScrollStarted) {
      // We don't want extra CaretStateChangedEvents dispatched when user is
      // scrolling the page.
      AC_LOG("%s: UpdateCarets(RespectOldAppearance | DispatchNoEvent)",
             __FUNCTION__);
      UpdateCarets({UpdateCaretsHint::RespectOldAppearance,
                    UpdateCaretsHint::DispatchNoEvent});
    } else {
      AC_LOG("%s: UpdateCarets(RespectOldAppearance)", __FUNCTION__);
      UpdateCarets(UpdateCaretsHint::RespectOldAppearance);
    }
  }
}

void AccessibleCaretManager::OnReflow() {
  nsAutoScriptBlocker scriptBlocker;
  AutoRestore<bool> saveAllowFlushingLayout(mLayoutFlusher.mAllowFlushing);
  mLayoutFlusher.mAllowFlushing = false;

  Maybe<PresShell::AutoAssertNoFlush> assert;
  if (mPresShell) {
    assert.emplace(*mPresShell);
  }

  if (mCarets.HasLogicallyVisibleCaret()) {
    AC_LOG("%s: UpdateCarets(RespectOldAppearance)", __FUNCTION__);
    UpdateCarets(UpdateCaretsHint::RespectOldAppearance);
  }
}

void AccessibleCaretManager::OnBlur() {
  AC_LOG("%s: HideCaretsAndDispatchCaretStateChangedEvent()", __FUNCTION__);
  HideCaretsAndDispatchCaretStateChangedEvent();
}

void AccessibleCaretManager::OnKeyboardEvent() {
  if (GetCaretMode() == CaretMode::Cursor) {
    AC_LOG("%s: HideCaretsAndDispatchCaretStateChangedEvent()", __FUNCTION__);
    HideCaretsAndDispatchCaretStateChangedEvent();
  }
}

void AccessibleCaretManager::OnFrameReconstruction() {
  mCarets.GetFirst()->EnsureApzAware();
  mCarets.GetSecond()->EnsureApzAware();
}

void AccessibleCaretManager::SetLastInputSource(uint16_t aInputSource) {
  mLastInputSource = aInputSource;
}

bool AccessibleCaretManager::ShouldDisableApz() const {
  return mDesiredAsyncPanZoomState.Get() ==
         DesiredAsyncPanZoomState::Value::Disabled;
}

Selection* AccessibleCaretManager::GetSelection() const {
  RefPtr<nsFrameSelection> fs = GetFrameSelection();
  if (!fs) {
    return nullptr;
  }
  return fs->GetSelection(SelectionType::eNormal);
}

already_AddRefed<nsFrameSelection> AccessibleCaretManager::GetFrameSelection()
    const {
  if (!mPresShell) {
    return nullptr;
  }

  // Prevent us from touching the nsFrameSelection associated with other
  // PresShell.
  RefPtr<nsFrameSelection> fs = mPresShell->GetLastFocusedFrameSelection();
  if (!fs || fs->GetPresShell() != mPresShell) {
    return nullptr;
  }

  return fs.forget();
}

nsAutoString AccessibleCaretManager::StringifiedSelection() const {
  nsAutoString str;
  RefPtr<Selection> selection = GetSelection();
  if (selection) {
    selection->Stringify(str, mLayoutFlusher.mAllowFlushing
                                  ? Selection::FlushFrames::Yes
                                  : Selection::FlushFrames::No);
  }
  return str;
}

// static
Element* AccessibleCaretManager::GetEditingHostForFrame(
    const nsIFrame* aFrame) {
  if (!aFrame) {
    return nullptr;
  }

  auto content = aFrame->GetContent();
  if (!content) {
    return nullptr;
  }

  return content->GetEditingHost();
}

AccessibleCaretManager::CaretMode AccessibleCaretManager::GetCaretMode() const {
  const Selection* selection = GetSelection();
  if (!selection) {
    return CaretMode::None;
  }

  const uint32_t rangeCount = selection->RangeCount();
  if (rangeCount <= 0) {
    return CaretMode::None;
  }

  const nsFocusManager* fm = nsFocusManager::GetFocusManager();
  MOZ_ASSERT(fm);
  if (fm->GetFocusedWindow() != mPresShell->GetDocument()->GetWindow()) {
    // Hide carets if the window is not focused.
    return CaretMode::None;
  }

  if (selection->IsCollapsed()) {
    return CaretMode::Cursor;
  }

  return CaretMode::Selection;
}

nsIFrame* AccessibleCaretManager::GetFocusableFrame(nsIFrame* aFrame) const {
  // This implementation is similar to EventStateManager::PostHandleEvent().
  // Look for the nearest enclosing focusable frame.
  nsIFrame* focusableFrame = aFrame;
  while (focusableFrame) {
    if (focusableFrame->IsFocusable(/* aWithMouse = */ true)) {
      break;
    }
    focusableFrame = focusableFrame->GetParent();
  }
  return focusableFrame;
}

void AccessibleCaretManager::ChangeFocusToOrClearOldFocus(
    nsIFrame* aFrame) const {
  RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
  MOZ_ASSERT(fm);

  if (aFrame) {
    nsIContent* focusableContent = aFrame->GetContent();
    MOZ_ASSERT(focusableContent, "Focusable frame must have content!");
    RefPtr<Element> focusableElement = Element::FromNode(focusableContent);
    fm->SetFocus(focusableElement, nsIFocusManager::FLAG_BYLONGPRESS);
  } else if (nsCOMPtr<nsPIDOMWindowOuter> win =
                 mPresShell->GetDocument()->GetWindow()) {
    fm->ClearFocus(win);
    fm->SetFocusedWindow(win);
  }
}

nsresult AccessibleCaretManager::SelectWord(nsIFrame* aFrame,
                                            const nsPoint& aPoint) const {
  AC_LOGV("%s", __FUNCTION__);

  SetSelectionDragState(true);
  const RefPtr<nsPresContext> pinnedPresContext{mPresShell->GetPresContext()};
  nsresult rs = aFrame->SelectByTypeAtPoint(pinnedPresContext, aPoint,
                                            eSelectWord, eSelectWord, 0);

  SetSelectionDragState(false);
  ClearMaintainedSelection();

  // Smart-select phone numbers if possible.
  if (StaticPrefs::layout_accessiblecaret_extend_selection_for_phone_number()) {
    SelectMoreIfPhoneNumber();
  }

  return rs;
}

void AccessibleCaretManager::SetSelectionDragState(bool aState) const {
  RefPtr<nsFrameSelection> fs = GetFrameSelection();
  if (fs) {
    fs->SetDragState(aState);
  }
}

bool AccessibleCaretManager::IsPhoneNumber(nsAString& aCandidate) const {
  RefPtr<Document> doc = mPresShell->GetDocument();
  nsAutoString phoneNumberRegex(u"(^\\+)?[0-9 ,\\-.()*#pw]{1,30}$"_ns);
  return nsContentUtils::IsPatternMatching(aCandidate, phoneNumberRegex, doc)
      .valueOr(false);
}

void AccessibleCaretManager::SelectMoreIfPhoneNumber() const {
  nsAutoString selectedText = StringifiedSelection();

  if (IsPhoneNumber(selectedText)) {
    SetSelectionDirection(eDirNext);
    ExtendPhoneNumberSelection(u"forward"_ns);

    SetSelectionDirection(eDirPrevious);
    ExtendPhoneNumberSelection(u"backward"_ns);

    SetSelectionDirection(eDirNext);
  }
}

void AccessibleCaretManager::ExtendPhoneNumberSelection(
    const nsAString& aDirection) const {
  if (!mPresShell) {
    return;
  }

  // Extend the phone number selection until we find a boundary.
  RefPtr<Selection> selection = GetSelection();

  while (selection) {
    const nsRange* anchorFocusRange = selection->GetAnchorFocusRange();
    if (!anchorFocusRange) {
      return;
    }

    // Backup the anchor focus range since both anchor node and focus node might
    // be changed after calling Selection::Modify().
    RefPtr<nsRange> oldAnchorFocusRange = anchorFocusRange->CloneRange();

    // Save current focus node, focus offset and the selected text so that
    // we can compare them with the modified ones later.
    nsINode* oldFocusNode = selection->GetFocusNode();
    uint32_t oldFocusOffset = selection->FocusOffset();
    nsAutoString oldSelectedText = StringifiedSelection();

    // Extend the selection by one char.
    selection->Modify(u"extend"_ns, aDirection, u"character"_ns,
                      IgnoreErrors());
    if (IsTerminated() == Terminated::Yes) {
      return;
    }

    // If the selection didn't change, (can't extend further), we're done.
    if (selection->GetFocusNode() == oldFocusNode &&
        selection->FocusOffset() == oldFocusOffset) {
      return;
    }

    // If the changed selection isn't a valid phone number, we're done.
    // Also, if the selection was extended to a new block node, the string
    // returned by stringify() won't have a new line at the beginning or the
    // end of the string. Therefore, if either focus node or offset is
    // changed, but selected text is not changed, we're done, too.
    nsAutoString selectedText = StringifiedSelection();

    if (!IsPhoneNumber(selectedText) || oldSelectedText == selectedText) {
      // Backout the undesired selection extend, restore the old anchor focus
      // range before exit.
      selection->SetAnchorFocusToRange(oldAnchorFocusRange);
      return;
    }
  }
}

void AccessibleCaretManager::SetSelectionDirection(nsDirection aDir) const {
  Selection* selection = GetSelection();
  if (selection) {
    selection->AdjustAnchorFocusForMultiRange(aDir);
  }
}

void AccessibleCaretManager::ClearMaintainedSelection() const {
  // Selection made by double-clicking for example will maintain the original
  // word selection. We should clear it so that we can drag caret freely.
  RefPtr<nsFrameSelection> fs = GetFrameSelection();
  if (fs) {
    fs->MaintainSelection(eSelectNoAmount);
  }
}

void AccessibleCaretManager::LayoutFlusher::MaybeFlush(
    const PresShell& aPresShell) {
  if (mAllowFlushing) {
    AutoRestore<bool> flushing(mFlushing);
    mFlushing = true;

    if (Document* doc = aPresShell.GetDocument()) {
      doc->FlushPendingNotifications(FlushType::Layout);
      // Don't access the PresShell after flushing, it could've become invalid.
    }
  }
}

nsIFrame* AccessibleCaretManager::GetFrameForFirstRangeStartOrLastRangeEnd(
    nsDirection aDirection, int32_t* aOutOffset, nsIContent** aOutContent,
    int32_t* aOutContentOffset) const {
  if (!mPresShell) {
    return nullptr;
  }

  MOZ_ASSERT(GetCaretMode() == CaretMode::Selection);
  MOZ_ASSERT(aOutOffset, "aOutOffset shouldn't be nullptr!");

  const nsRange* range = nullptr;
  RefPtr<nsINode> startNode;
  RefPtr<nsINode> endNode;
  int32_t nodeOffset = 0;
  CaretAssociationHint hint;

  RefPtr<Selection> selection = GetSelection();
  bool findInFirstRangeStart = aDirection == eDirNext;

  if (findInFirstRangeStart) {
    range = selection->GetRangeAt(0);
    startNode = range->GetStartContainer();
    endNode = range->GetEndContainer();
    nodeOffset = range->StartOffset();
    hint = CARET_ASSOCIATE_AFTER;
  } else {
    MOZ_ASSERT(selection->RangeCount() > 0);
    range = selection->GetRangeAt(selection->RangeCount() - 1);
    startNode = range->GetEndContainer();
    endNode = range->GetStartContainer();
    nodeOffset = range->EndOffset();
    hint = CARET_ASSOCIATE_BEFORE;
  }

  nsCOMPtr<nsIContent> startContent = do_QueryInterface(startNode);
  nsIFrame* startFrame = nsFrameSelection::GetFrameForNodeOffset(
      startContent, nodeOffset, hint, aOutOffset);

  if (!startFrame) {
    ErrorResult err;
    RefPtr<TreeWalker> walker = mPresShell->GetDocument()->CreateTreeWalker(
        *startNode, dom::NodeFilter_Binding::SHOW_ALL, nullptr, err);

    if (!walker) {
      return nullptr;
    }

    startFrame = startContent ? startContent->GetPrimaryFrame() : nullptr;
    while (!startFrame && startNode != endNode) {
      startNode = findInFirstRangeStart ? walker->NextNode(err)
                                        : walker->PreviousNode(err);

      if (!startNode) {
        break;
      }

      startContent = startNode->AsContent();
      startFrame = startContent ? startContent->GetPrimaryFrame() : nullptr;
    }

    // We are walking among the nodes in the content tree, so the node offset
    // relative to startNode should be set to 0.
    nodeOffset = 0;
    *aOutOffset = 0;
  }

  if (startFrame) {
    if (aOutContent) {
      startContent.forget(aOutContent);
    }
    if (aOutContentOffset) {
      *aOutContentOffset = nodeOffset;
    }
  }

  return startFrame;
}

bool AccessibleCaretManager::RestrictCaretDraggingOffsets(
    nsIFrame::ContentOffsets& aOffsets) {
  if (!mPresShell) {
    return false;
  }

  MOZ_ASSERT(GetCaretMode() == CaretMode::Selection);

  nsDirection dir =
      mActiveCaret == mCarets.GetFirst() ? eDirPrevious : eDirNext;
  int32_t offset = 0;
  nsCOMPtr<nsIContent> content;
  int32_t contentOffset = 0;
  nsIFrame* frame = GetFrameForFirstRangeStartOrLastRangeEnd(
      dir, &offset, getter_AddRefs(content), &contentOffset);

  if (!frame) {
    return false;
  }

  // Compare the active caret's new position (aOffsets) to the inactive caret's
  // position.
  NS_ASSERTION(contentOffset >= 0, "contentOffset should not be negative");
  const Maybe<int32_t> cmpToInactiveCaretPos =
      nsContentUtils::ComparePoints_AllowNegativeOffsets(
          aOffsets.content, aOffsets.StartOffset(), content, contentOffset);
  if (NS_WARN_IF(!cmpToInactiveCaretPos)) {
    // Potentially handle this properly when Selection across Shadow DOM
    // boundary is implemented
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=1607497).
    return false;
  }

  // Move one character (in the direction of dir) from the inactive caret's
  // position. This is the limit for the active caret's new position.
  PeekOffsetStruct limit(
      eSelectCluster, dir, offset, nsPoint(0, 0),
      {PeekOffsetOption::JumpLines, PeekOffsetOption::ScrollViewStop});
  nsresult rv = frame->PeekOffset(&limit);
  if (NS_FAILED(rv)) {
    limit.mResultContent = content;
    limit.mContentOffset = contentOffset;
  }

  // Compare the active caret's new position (aOffsets) to the limit.
  NS_ASSERTION(limit.mContentOffset >= 0,
               "limit.mContentOffset should not be negative");
  const Maybe<int32_t> cmpToLimit =
      nsContentUtils::ComparePoints_AllowNegativeOffsets(
          aOffsets.content, aOffsets.StartOffset(), limit.mResultContent,
          limit.mContentOffset);
  if (NS_WARN_IF(!cmpToLimit)) {
    // Potentially handle this properly when Selection across Shadow DOM
    // boundary is implemented
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=1607497).
    return false;
  }

  auto SetOffsetsToLimit = [&aOffsets, &limit]() {
    aOffsets.content = limit.mResultContent;
    aOffsets.offset = limit.mContentOffset;
    aOffsets.secondaryOffset = limit.mContentOffset;
  };

  if (!StaticPrefs::
          layout_accessiblecaret_allow_dragging_across_other_caret()) {
    if ((mActiveCaret == mCarets.GetFirst() && *cmpToLimit == 1) ||
        (mActiveCaret == mCarets.GetSecond() && *cmpToLimit == -1)) {
      // The active caret's position is past the limit, which we don't allow
      // here. So set it to the limit, resulting in one character being
      // selected.
      SetOffsetsToLimit();
    }
  } else {
    switch (*cmpToInactiveCaretPos) {
      case 0:
        // The active caret's position is the same as the position of the
        // inactive caret. So set it to the limit to prevent the selection from
        // being collapsed, resulting in one character being selected.
        SetOffsetsToLimit();
        break;
      case 1:
        if (mActiveCaret == mCarets.GetFirst()) {
          // First caret was moved across the second caret. After making change
          // to the selection, the user will drag the second caret.
          mActiveCaret = mCarets.GetSecond();
        }
        break;
      case -1:
        if (mActiveCaret == mCarets.GetSecond()) {
          // Second caret was moved across the first caret. After making change
          // to the selection, the user will drag the first caret.
          mActiveCaret = mCarets.GetFirst();
        }
        break;
    }
  }

  return true;
}

bool AccessibleCaretManager::CompareTreePosition(nsIFrame* aStartFrame,
                                                 nsIFrame* aEndFrame) const {
  return (aStartFrame && aEndFrame &&
          nsLayoutUtils::CompareTreePosition(aStartFrame, aEndFrame) <= 0);
}

nsresult AccessibleCaretManager::DragCaretInternal(const nsPoint& aPoint) {
  MOZ_ASSERT(mPresShell);

  nsIFrame* rootFrame = mPresShell->GetRootFrame();
  MOZ_ASSERT(rootFrame, "We need root frame to compute caret dragging!");

  nsPoint point = AdjustDragBoundary(
      nsPoint(aPoint.x, aPoint.y + mOffsetYToCaretLogicalPosition));

  // Find out which content we point to

  nsIFrame* ptFrame = nsLayoutUtils::GetFrameForPoint(
      RelativeTo{rootFrame}, point, GetHitTestOptions());
  if (!ptFrame) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsFrameSelection> fs = GetFrameSelection();
  MOZ_ASSERT(fs);

  nsresult result;
  nsIFrame* newFrame = nullptr;
  nsPoint newPoint;
  nsPoint ptInFrame = point;
  nsLayoutUtils::TransformPoint(RelativeTo{rootFrame}, RelativeTo{ptFrame},
                                ptInFrame);
  result = fs->ConstrainFrameAndPointToAnchorSubtree(ptFrame, ptInFrame,
                                                     &newFrame, newPoint);
  if (NS_FAILED(result) || !newFrame) {
    return NS_ERROR_FAILURE;
  }

  if (!newFrame->IsSelectable(nullptr)) {
    return NS_ERROR_FAILURE;
  }

  nsIFrame::ContentOffsets offsets =
      newFrame->GetContentOffsetsFromPoint(newPoint);
  if (offsets.IsNull()) {
    return NS_ERROR_FAILURE;
  }

  if (GetCaretMode() == CaretMode::Selection &&
      !RestrictCaretDraggingOffsets(offsets)) {
    return NS_ERROR_FAILURE;
  }

  ClearMaintainedSelection();

  const nsFrameSelection::FocusMode focusMode =
      (GetCaretMode() == CaretMode::Selection)
          ? nsFrameSelection::FocusMode::kExtendSelection
          : nsFrameSelection::FocusMode::kCollapseToNewPoint;
  fs->HandleClick(MOZ_KnownLive(offsets.content) /* bug 1636889 */,
                  offsets.StartOffset(), offsets.EndOffset(), focusMode,
                  offsets.associate);
  return NS_OK;
}

// static
nsRect AccessibleCaretManager::GetAllChildFrameRectsUnion(nsIFrame* aFrame) {
  nsRect unionRect;

  // Drill through scroll frames, we don't want to include scrollbar child
  // frames below.
  for (nsIFrame* frame = aFrame->GetContentInsertionFrame(); frame;
       frame = frame->GetNextContinuation()) {
    nsRect frameRect;

    for (const auto& childList : frame->ChildLists()) {
      // Loop all children to union their scrollable overflow rect.
      for (nsIFrame* child : childList.mList) {
        nsRect childRect = child->ScrollableOverflowRectRelativeToSelf();
        nsLayoutUtils::TransformRect(child, frame, childRect);

        // A TextFrame containing only '\n' has positive height and width 0, or
        // positive width and height 0 if it's vertical. Need to use UnionEdges
        // to add its rect. BRFrame rect should be non-empty.
        if (childRect.IsEmpty()) {
          frameRect = frameRect.UnionEdges(childRect);
        } else {
          frameRect = frameRect.Union(childRect);
        }
      }
    }

    MOZ_ASSERT(!frameRect.IsEmpty(),
               "Editable frames should have at least one BRFrame child to make "
               "frameRect non-empty!");
    if (frame != aFrame) {
      nsLayoutUtils::TransformRect(frame, aFrame, frameRect);
    }
    unionRect = unionRect.Union(frameRect);
  }

  return unionRect;
}

nsPoint AccessibleCaretManager::AdjustDragBoundary(
    const nsPoint& aPoint) const {
  nsPoint adjustedPoint = aPoint;

  int32_t focusOffset = 0;
  nsIFrame* focusFrame =
      nsCaret::GetFrameAndOffset(GetSelection(), nullptr, 0, &focusOffset);
  Element* editingHost = GetEditingHostForFrame(focusFrame);

  if (editingHost) {
    nsIFrame* editingHostFrame = editingHost->GetPrimaryFrame();
    if (editingHostFrame) {
      nsRect boundary =
          AccessibleCaretManager::GetAllChildFrameRectsUnion(editingHostFrame);
      nsLayoutUtils::TransformRect(editingHostFrame, mPresShell->GetRootFrame(),
                                   boundary);

      // Shrink the rect to make sure we never hit the boundary.
      boundary.Deflate(kBoundaryAppUnits);

      adjustedPoint = boundary.ClampPoint(adjustedPoint);
    }
  }

  if (GetCaretMode() == CaretMode::Selection &&
      !StaticPrefs::
          layout_accessiblecaret_allow_dragging_across_other_caret()) {
    // Bug 1068474: Adjust the Y-coordinate so that the carets won't be in tilt
    // mode when a caret is being dragged surpass the other caret.
    //
    // For example, when dragging the second caret, the horizontal boundary
    // (lower bound) of its Y-coordinate is the logical position of the first
    // caret. Likewise, when dragging the first caret, the horizontal boundary
    // (upper bound) of its Y-coordinate is the logical position of the second
    // caret.
    if (mActiveCaret == mCarets.GetFirst()) {
      nscoord dragDownBoundaryY = mCarets.GetSecond()->LogicalPosition().y;
      if (dragDownBoundaryY > 0 && adjustedPoint.y > dragDownBoundaryY) {
        adjustedPoint.y = dragDownBoundaryY;
      }
    } else {
      nscoord dragUpBoundaryY = mCarets.GetFirst()->LogicalPosition().y;
      if (adjustedPoint.y < dragUpBoundaryY) {
        adjustedPoint.y = dragUpBoundaryY;
      }
    }
  }

  return adjustedPoint;
}

void AccessibleCaretManager::StartSelectionAutoScrollTimer(
    const nsPoint& aPoint) const {
  Selection* selection = GetSelection();
  MOZ_ASSERT(selection);

  nsIFrame* anchorFrame = selection->GetPrimaryFrameForAnchorNode();
  if (!anchorFrame) {
    return;
  }

  nsIScrollableFrame* scrollFrame = nsLayoutUtils::GetNearestScrollableFrame(
      anchorFrame, nsLayoutUtils::SCROLLABLE_SAME_DOC |
                       nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN);
  if (!scrollFrame) {
    return;
  }

  nsIFrame* capturingFrame = scrollFrame->GetScrolledFrame();
  if (!capturingFrame) {
    return;
  }

  nsIFrame* rootFrame = mPresShell->GetRootFrame();
  MOZ_ASSERT(rootFrame);
  nsPoint ptInScrolled = aPoint;
  nsLayoutUtils::TransformPoint(RelativeTo{rootFrame},
                                RelativeTo{capturingFrame}, ptInScrolled);

  RefPtr<nsFrameSelection> fs = GetFrameSelection();
  MOZ_ASSERT(fs);
  fs->StartAutoScrollTimer(capturingFrame, ptInScrolled, kAutoScrollTimerDelay);
}

void AccessibleCaretManager::StopSelectionAutoScrollTimer() const {
  RefPtr<nsFrameSelection> fs = GetFrameSelection();
  MOZ_ASSERT(fs);
  fs->StopAutoScrollTimer();
}

void AccessibleCaretManager::DispatchCaretStateChangedEvent(
    CaretChangedReason aReason, const nsPoint* aPoint) {
  if (MaybeFlushLayout() == Terminated::Yes) {
    return;
  }

  const Selection* sel = GetSelection();
  if (!sel) {
    return;
  }

  Document* doc = mPresShell->GetDocument();
  MOZ_ASSERT(doc);

  CaretStateChangedEventInit init;
  init.mBubbles = true;

  const nsRange* range = sel->GetAnchorFocusRange();
  nsINode* commonAncestorNode = nullptr;
  if (range) {
    commonAncestorNode = range->GetClosestCommonInclusiveAncestor();
  }

  if (!commonAncestorNode) {
    commonAncestorNode = sel->GetFrameSelection()->GetAncestorLimiter();
  }

  RefPtr<DOMRect> domRect = new DOMRect(ToSupports(doc));
  nsRect rect = nsLayoutUtils::GetSelectionBoundingRect(sel);

  nsIFrame* commonAncestorFrame = nullptr;
  nsIFrame* rootFrame = mPresShell->GetRootFrame();

  if (commonAncestorNode && commonAncestorNode->IsContent()) {
    commonAncestorFrame = commonAncestorNode->AsContent()->GetPrimaryFrame();
  }

  if (commonAncestorFrame && rootFrame) {
    nsLayoutUtils::TransformRect(rootFrame, commonAncestorFrame, rect);
    nsRect clampedRect =
        nsLayoutUtils::ClampRectToScrollFrames(commonAncestorFrame, rect);
    nsLayoutUtils::TransformRect(commonAncestorFrame, rootFrame, clampedRect);
    rect = clampedRect;
    init.mSelectionVisible = !clampedRect.IsEmpty();
  } else {
    init.mSelectionVisible = true;
  }

  domRect->SetLayoutRect(rect);

  // Send isEditable info w/ event detail. This info can help determine
  // whether to show cut command on selection dialog or not.
  init.mSelectionEditable =
      commonAncestorFrame && GetEditingHostForFrame(commonAncestorFrame);

  init.mBoundingClientRect = domRect;
  init.mReason = aReason;
  init.mCollapsed = sel->IsCollapsed();
  init.mCaretVisible = mCarets.HasLogicallyVisibleCaret();
  init.mCaretVisuallyVisible = mCarets.HasVisuallyVisibleCaret();
  init.mSelectedTextContent = StringifiedSelection();

  if (aPoint) {
    CSSIntPoint pt = CSSPixel::FromAppUnitsRounded(*aPoint);
    init.mClientX = pt.x;
    init.mClientY = pt.y;
  }

  RefPtr<CaretStateChangedEvent> event = CaretStateChangedEvent::Constructor(
      doc, u"mozcaretstatechanged"_ns, init);

  event->SetTrusted(true);
  event->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;

  AC_LOG("%s: reason %" PRIu32 ", collapsed %d, caretVisible %" PRIu32,
         __FUNCTION__, static_cast<uint32_t>(init.mReason), init.mCollapsed,
         static_cast<uint32_t>(init.mCaretVisible));

  (new AsyncEventDispatcher(doc, event))->PostDOMEvent();
}

AccessibleCaretManager::Carets::Carets(UniquePtr<AccessibleCaret> aFirst,
                                       UniquePtr<AccessibleCaret> aSecond)
    : mFirst{std::move(aFirst)}, mSecond{std::move(aSecond)} {}

}  // namespace mozilla
