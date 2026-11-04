/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTextControlFrame.h"

#include <algorithm>

#include "ErrorList.h"
#include "PseudoStyleType.h"
#include "gfxContext.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/IMEContentObserver.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/TextEditor.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLTextAreaElement.h"
#include "mozilla/dom/Selection.h"
#include "nsCaret.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsFocusManager.h"
#include "nsFontMetrics.h"
#include "nsFrameSelection.h"
#include "nsGenericHTMLElement.h"
#include "nsIContent.h"
#include "nsIEditor.h"
#include "nsINode.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"

using namespace mozilla;
using namespace mozilla::dom;

nsTextControlFrame* NS_NewTextControlFrame(PresShell* aPresShell,
                                           ComputedStyle* aStyle) {
  return new nsTextControlFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsTextControlFrame)

NS_QUERYFRAME_HEAD(nsTextControlFrame)
  NS_QUERYFRAME_ENTRY(nsTextControlFrame)
NS_QUERYFRAME_TAIL_INHERITING(ScrollContainerFrame)

#ifdef ACCESSIBILITY
a11y::AccType nsTextControlFrame::AccessibleType() {
  if (ControlElement()->ControlType() == FormControlType::InputNumber) {
    return a11y::eHTMLSpinnerType;
  }
  return a11y::eHTMLTextFieldType;
}
#endif

nsTextControlFrame::nsTextControlFrame(ComputedStyle* aStyle,
                                       nsPresContext* aPresContext)
    : ScrollContainerFrame(aStyle, aPresContext, kClassID,
                           /* aIsRoot = */ false) {}

nsTextControlFrame::~nsTextControlFrame() = default;

Element* nsTextControlFrame::GetButton() const { return mButtonContent; }

nsIFrame* nsTextControlFrame::GetButtonBoxFrame() const {
  return mButtonContent ? mButtonContent->GetPrimaryFrame() : nullptr;
}

nsresult nsTextControlFrame::CreateAnonymousContent(
    nsTArray<ContentInfo>& aElements) {
  nsresult rv = ScrollContainerFrame::CreateAnonymousContent(aElements);
  NS_ENSURE_SUCCESS(rv, rv);
  mButtonContent = ControlElement()->CreateButton();
  if (mButtonContent) {
    aElements.AppendElement(mButtonContent);
  }
  return NS_OK;
}

void nsTextControlFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  switch (StyleUIReset()->mFieldSizing) {
    case StyleFieldSizing::Content:
      RemoveStateBits(NS_FRAME_REFLOW_ROOT);
      break;
    case StyleFieldSizing::Fixed:
      // Mark the input as being a reflow root. This will allow incremental
      // reflows to be initiated at this frame, rather than descending from the
      // root frame of the frame hierarchy.
      AddStateBits(NS_FRAME_REFLOW_ROOT);
      break;
  }
  ScrollContainerFrame::DidSetComputedStyle(aOldComputedStyle);
}

void nsTextControlFrame::AppendAnonymousContentTo(
    nsTArray<nsIContent*>& aElements, uint32_t aFilter) {
  ScrollContainerFrame::AppendAnonymousContentTo(aElements, aFilter);
  if (mButtonContent) {
    aElements.AppendElement(mButtonContent);
  }
}

void nsTextControlFrame::InitPrimaryFrame() {
  if (auto* ts = ControlElement()->GetTextControlState()) {
    ts->InitializeSelection(PresShell());
  }
  nsIFrame::InitPrimaryFrame();
}

void nsTextControlFrame::Destroy(DestroyContext& aContext) {
  if (auto* ts = ControlElement()->GetTextControlState()) {
    ts->DeinitSelection();
  }
  aContext.AddAnonymousContent(mButtonContent.forget());
  ScrollContainerFrame::Destroy(aContext);
}

LogicalSize nsTextControlFrame::CalcFixedSize(gfxContext* aRenderingContext,
                                              WritingMode aWM) const {
  MOZ_ASSERT(StyleUIReset()->mFieldSizing == StyleFieldSizing::Fixed);

  LogicalSize intrinsicSize(aWM);
  const float inflation = nsLayoutUtils::FontSizeInflationFor(this);
  RefPtr<nsFontMetrics> fontMet =
      nsLayoutUtils::GetFontMetricsForFrame(this, inflation);
  const nscoord lineHeight = ReflowInput::CalcLineHeight(
      *Style(), PresContext(), GetContent(), inflation);
  // Use the larger of the font's "average" char width or the width of the
  // zero glyph (if present) as the basis for resolving the size attribute.
  const nscoord charWidth =
      std::max(fontMet->ZeroOrAveCharWidth(), fontMet->AveCharWidth());
  const nscoord charMaxAdvance = fontMet->MaxAdvance();

  // Initialize based on the width in characters.
  const Maybe<int32_t> maybeCols = GetCols();
  const int32_t cols = maybeCols.valueOr(TextControlElement::DEFAULT_COLS);
  intrinsicSize.ISize(aWM) = cols * charWidth;

  // If we do not have what appears to be a fixed-width font, add a "slop"
  // amount based on the max advance of the font (clamped to twice charWidth,
  // because some fonts have a few extremely-wide outliers that would result
  // in excessive width here; e.g. the triple-emdash ligature in SFNS Text),
  // minus 4px. This helps avoid input fields becoming unusably narrow with
  // small size values.
  if (charMaxAdvance - charWidth > AppUnitsPerCSSPixel()) {
    nscoord internalPadding =
        std::max(0, std::min(charMaxAdvance, charWidth * 2) -
                        nsPresContext::CSSPixelsToAppUnits(4));
    internalPadding = RoundToMultiple(internalPadding, AppUnitsPerCSSPixel());
    intrinsicSize.ISize(aWM) += internalPadding;
  }

  // Increment width with cols * letter-spacing.
  {
    const auto& letterSpacing = StyleText()->mLetterSpacing;
    if (!letterSpacing.IsDefinitelyZero()) {
      intrinsicSize.ISize(aWM) +=
          cols * letterSpacing.Resolve(fontMet->EmHeight());
    }
  }

  // Set the height equal to total number of rows (times the height of each
  // line, of course)
  intrinsicSize.BSize(aWM) = lineHeight * GetRows();

  // Add in the size of the scrollbars for textarea
  if (IsTextArea()) {
    if (ScrollContainerFrame* scrollContainerFrame = GetScrollTargetFrame()) {
      LogicalMargin scrollbarSizes(
          aWM, scrollContainerFrame->GetDesiredScrollbarSizes());
      intrinsicSize.ISize(aWM) += scrollbarSizes.IStartEnd(aWM);

      // We only include scrollbar-thickness in our BSize if the scrollbar on
      // that side is explicitly forced-to-be-present.
      const bool includeScrollbarBSize = [&] {
        if (!StaticPrefs::
                layout_forms_textarea_sizing_excludes_auto_scrollbar_enabled()) {
          return true;
        }
        auto overflow = aWM.IsVertical() ? StyleDisplay()->mOverflowY
                                         : StyleDisplay()->mOverflowX;
        return overflow == StyleOverflow::Scroll;
      }();
      if (includeScrollbarBSize) {
        intrinsicSize.BSize(aWM) += scrollbarSizes.BStartEnd(aWM);
      }
    }
  }

  // Add the inline size of the button if our char size is explicit, so as to
  // make sure to make enough space for it.
  if (maybeCols.isSome()) {
    if (auto* button = GetButton(); button && button->GetPrimaryFrame()) {
      const IntrinsicSizeInput input(aRenderingContext, Nothing(), Nothing());
      intrinsicSize.ISize(aWM) += button->GetPrimaryFrame()->GetMinISize(input);
    }
  }

  return intrinsicSize;
}

nscoord nsTextControlFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                           IntrinsicISizeType aType) {
  if (StyleUIReset()->mFieldSizing == StyleFieldSizing::Content) {
    return ScrollContainerFrame::IntrinsicISize(aInput, aType);
  }
  // Our min inline size is just our preferred inline-size if we have auto
  // inline size.
  WritingMode wm = GetWritingMode();
  return CalcFixedSize(aInput.mContext, wm).ISize(wm);
}

Maybe<nscoord> nsTextControlFrame::ComputeBaseline(
    const nsIFrame* aFrame, const ReflowInput& aReflowInput,
    bool aForSingleLineControl) {
  // If we're layout-contained, we have no baseline.
  if (aReflowInput.mStyleDisplay->IsContainLayout()) {
    return Nothing();
  }
  WritingMode wm = aReflowInput.GetWritingMode();

  nscoord lineHeight = aReflowInput.ComputedBSize();
  if (!aForSingleLineControl || lineHeight == NS_UNCONSTRAINEDSIZE) {
    lineHeight = aReflowInput.ApplyMinMaxBSize(aReflowInput.GetLineHeight());
  }
  RefPtr<nsFontMetrics> fontMet =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(aFrame);
  return Some(nsLayoutUtils::GetCenteredFontBaseline(fontMet, lineHeight,
                                                     wm.IsLineInverted()) +
              aReflowInput.ComputedLogicalBorderPadding(wm).BStart(wm));
}

void nsTextControlFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aDesiredSize,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  DO_GLOBAL_REFLOW_COUNT("nsTextControlFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  MOZ_ASSERT_IF(StyleUIReset()->mFieldSizing == StyleFieldSizing::Fixed,
                HasAnyStateBits(NS_FRAME_REFLOW_ROOT));
  {
    // Calculate the baseline and store it in mFirstBaseline.
    // TODO(emilio): Is this still needed?
    auto baseline =
        ComputeBaseline(this, aReflowInput, IsSingleLineTextControl());
    mFirstBaseline = baseline.valueOr(NS_INTRINSIC_ISIZE_UNKNOWN);
    if (baseline) {
      aDesiredSize.SetBlockStartAscent(*baseline);
    }
  }

  // FIXME(emilio): This is rather hacky, but matches how nsBlockFrame tweaks
  // avail bsize. Maybe do a copy instead, or plumb stuff further down but...
  const auto oldBSize = aReflowInput.ComputedBSize();
  const WritingMode wm = aReflowInput.GetWritingMode();
  if (oldBSize == NS_UNCONSTRAINEDSIZE &&
      StyleUIReset()->mFieldSizing != StyleFieldSizing::Content) {
    const nscoord fixedBSize = aReflowInput.ApplyMinMaxBSize(
        CalcFixedSize(aReflowInput.mRenderingContext, wm).BSize(wm));
    const_cast<ReflowInput&>(aReflowInput)
        .SetComputedBSize(fixedBSize, ReflowInput::ResetResizeFlags::No);
  }
  ScrollContainerFrame::Reflow(aPresContext, aDesiredSize, aReflowInput,
                               aStatus);
  const_cast<ReflowInput&>(aReflowInput)
      .SetComputedBSize(oldBSize, ReflowInput::ResetResizeFlags::No);
}

void nsTextControlFrame::HandleReadonlyOrDisabledChange() {
  RefPtr<TextControlElement> el = ControlElement();
  const RefPtr<TextEditor> editor = el->GetExtantTextEditor();
  if (!editor) {
    return;
  }
  nsISelectionController* const selCon = el->GetSelectionController();
  if (!selCon) {
    return;
  }
  if (el->IsDisabledOrReadOnly()) {
    if (nsFocusManager::GetFocusedElementStatic() == el) {
      selCon->SetCaretEnabled(false);
    }
    editor->AddFlags(nsIEditor::eEditorReadonlyMask);
  } else {
    if (nsFocusManager::GetFocusedElementStatic() == el) {
      selCon->SetCaretEnabled(true);
    }
    editor->RemoveFlags(nsIEditor::eEditorReadonlyMask);
  }
}

void nsTextControlFrame::ElementStateChanged(dom::ElementState aStates) {
  if (aStates.HasAtLeastOneOfStates(dom::ElementState::READONLY |
                                    dom::ElementState::DISABLED)) {
    HandleReadonlyOrDisabledChange();
  }
  // When the control loses focus, do not let a pending selection scroll scroll
  // the ancestors. Do not revoke it, because it can also scroll the caret into
  // view inside the control.
  if (aStates.HasState(dom::ElementState::FOCUS) &&
      !ControlElement()->State().HasState(dom::ElementState::FOCUS)) {
    if (nsFrameSelection* frameSelection = GetOwnedFrameSelection()) {
      frameSelection->NormalSelection().LimitPendingScrollToFirstAncestor();
    }
  }
  return ScrollContainerFrame::ElementStateChanged(aStates);
}

nsresult nsTextControlFrame::PeekOffset(PeekOffsetStruct* aPos) {
  return NS_ERROR_FAILURE;
}

Maybe<nscoord> nsTextControlFrame::GetNaturalBaselineBOffset(
    mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext aExportContext) const {
  if (!IsSingleLineTextControl()) {
    if (StyleDisplay()->IsContainLayout()) {
      return Nothing{};
    }

    if (aBaselineGroup == BaselineSharingGroup::First) {
      return Some(CSSMinMax(mFirstBaseline, 0, BSize(aWM)));
    }
    // This isn't great, but the content of the root NAC isn't guaranteed
    // to be loaded, so the best we can do is the edge of the border-box.
    if (aWM.IsCentralBaseline()) {
      return Some(BSize(aWM) / 2);
    }
    return Some(0);
  }
  NS_ASSERTION(!IsSubtreeDirty(), "frame must not be dirty");
  return GetSingleLineTextControlBaseline(this, mFirstBaseline, aWM,
                                          aBaselineGroup);
}
