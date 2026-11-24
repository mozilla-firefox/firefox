/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsListControlFrame.h"

#include <algorithm>

#include "mozilla/LookAndFeel.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/dom/HTMLOptGroupElement.h"
#include "mozilla/dom/HTMLOptionsCollection.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "nsCSSRendering.h"
#include "nsComboboxControlFrame.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsFontMetrics.h"
#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"
#include "nscore.h"

using namespace mozilla;
using namespace mozilla::dom;

//---------------------------------------------------------
nsListControlFrame* NS_NewListControlFrame(PresShell* aPresShell,
                                           ComputedStyle* aStyle) {
  return new nsListControlFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsListControlFrame)

nsListControlFrame::nsListControlFrame(ComputedStyle* aStyle,
                                       nsPresContext* aPresContext)
    : ScrollContainerFrame(aStyle, aPresContext, kClassID, false) {}

nsListControlFrame::~nsListControlFrame() = default;

Maybe<nscoord> nsListControlFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  // Unlike scroll frames which we inherit from, we don't export a baseline.
  return Nothing{};
}

HTMLOptionElement* nsListControlFrame::GetCurrentOption() const {
  return Select().GetCurrentOption();
}

bool nsListControlFrame::IsFocused() const {
  return Select().State().HasState(ElementState::FOCUS);
}

void nsListControlFrame::InvalidateFocus() { InvalidateFrame(); }

NS_QUERYFRAME_HEAD(nsListControlFrame)
  NS_QUERYFRAME_ENTRY(nsListControlFrame)
NS_QUERYFRAME_TAIL_INHERITING(ScrollContainerFrame)

#ifdef ACCESSIBILITY
a11y::AccType nsListControlFrame::AccessibleType() {
  return a11y::eHTMLSelectListType;
}
#endif

// Return true if we found at least one <option> or non-empty <optgroup> label
// that has a frame.  aResult will be the maximum BSize of those.
static bool GetMaxRowBSize(nsIFrame* aContainer, WritingMode aWM,
                           nscoord* aResult) {
  bool found = false;
  for (nsIFrame* child : aContainer->PrincipalChildList()) {
    nsIContent* content = child->GetContent();
    bool isOptGroupLabel =
        child->Style()->IsPseudoElement() &&
        aContainer->GetContent()->IsHTMLElement(nsGkAtoms::optgroup);
    if (!isOptGroupLabel && content->IsElement() &&
        !content->IsAnyOfHTMLElements(nsGkAtoms::option, nsGkAtoms::hr)) {
      // An optgroup or a wrapper element, both of which can contain options;
      // drill through any scroll frame and recurse.  |inner| might be null here
      // though if |inner| is an anonymous leaf frame of some sort.
      auto inner = child->GetContentInsertionFrame();
      if (inner && GetMaxRowBSize(inner, aWM, aResult)) {
        found = true;
      }
      continue;
    }
    // an option, an <hr> or an optgroup label
    nscoord childBSize = child->BSize(aWM);
    // XXX bug 1499176: skip empty <optgroup> labels (zero bsize) for now
    if (!isOptGroupLabel || childBSize > nscoord(0)) {
      found = true;
      *aResult = std::max(childBSize, *aResult);
    }
  }
  return found;
}

//-----------------------------------------------------------------
// Main Reflow for ListBox/Dropdown
//-----------------------------------------------------------------

nscoord nsListControlFrame::CalcBSizeOfARow() {
  // Calculate the block size in our writing mode of a single row in the
  // listbox or dropdown list by using the tallest thing in the subtree,
  // since there may be option groups in addition to option elements,
  // either of which may be visible or invisible, may use different
  // fonts, etc.
  nscoord rowBSize(0);
  if (GetContainSizeAxes().mBContained ||
      !GetMaxRowBSize(GetContentInsertionFrame(), GetWritingMode(),
                      &rowBSize)) {
    // We don't have any <option>s or <optgroup> labels with a frame.
    // (Or we're size-contained in block axis, which has the same outcome for
    // our sizing.)
    float inflation = nsLayoutUtils::FontSizeInflationFor(this);
    rowBSize = CalcFallbackRowBSize(inflation);
  }
  return rowBSize;
}

nscoord nsListControlFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                           IntrinsicISizeType aType) {
  // Always add scrollbar inline sizes to the intrinsic isize of the
  // scrolled content. Combobox frames depend on this happening in the
  // dropdown, and standalone listboxes are overflow:scroll so they need
  // it too.
  WritingMode wm = GetWritingMode();
  nscoord result;
  if (Maybe<nscoord> containISize = ContainIntrinsicISize()) {
    result = *containISize;
  } else {
    result = GetScrolledFrame()->IntrinsicISize(aInput, aType);
  }
  LogicalMargin scrollbarSize(wm, GetDesiredScrollbarSizes());
  result = NSCoordSaturatingAdd(result, scrollbarSize.IStartEnd(wm));
  return result;
}

void nsListControlFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aDesiredSize,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_WARNING_ASSERTION(aReflowInput.ComputedISize() != NS_UNCONSTRAINEDSIZE,
                       "Must have a computed inline size");
  SchedulePaint();

  MarkInReflow();
  // Due to the fact that our intrinsic block size depends on the block
  // sizes of our kids, we end up having to do two-pass reflow, in
  // general -- the first pass to find the intrinsic block size and a
  // second pass to reflow the scrollframe at that block size (which
  // will size the scrollbars correctly, etc).
  //
  // Naturally, we want to avoid doing the second reflow as much as
  // possible. We can skip it in the following cases (in all of which the first
  // reflow is already happening at the right block size):
  bool autoBSize = (aReflowInput.ComputedBSize() == NS_UNCONSTRAINEDSIZE);
  Maybe<nscoord> containBSize = ContainIntrinsicBSize(NS_UNCONSTRAINEDSIZE);
  bool usingContainBSize =
      autoBSize && containBSize && *containBSize != NS_UNCONSTRAINEDSIZE;

  mMightNeedSecondPass = [&] {
    if (!autoBSize) {
      // We're reflowing with a constrained computed block size -- just use that
      // block size.
      return false;
    }
    if (!IsSubtreeDirty() && !aReflowInput.ShouldReflowAllKids()) {
      // We're not dirty and have no dirty kids and shouldn't be reflowing all
      // kids. In this case, our cached max block size of a child is not going
      // to change.
      return false;
    }
    if (usingContainBSize) {
      // We're size-contained in the block axis. In this case the size of a row
      // doesn't depend on our children (it's the "fallback" size).
      return false;
    }
    // We might need to do a second pass. If we do our first reflow using our
    // cached max block size of a child, then compute the new max block size,
    // and it's the same as the old one, we might still skip it (see the
    // IsScrollbarUpdateSuppressed() check).
    return true;
  }();

  ReflowInput state(aReflowInput);
  int32_t length = GetNumberOfRows();

  nscoord oldBSizeOfARow = BSizeOfARow();

  if (!HasAnyStateBits(NS_FRAME_FIRST_REFLOW) && autoBSize) {
    // When not doing an initial reflow, and when the block size is
    // auto, start off with our computed block size set to what we'd
    // expect our block size to be.
    nscoord computedBSize = CalcIntrinsicBSize(oldBSizeOfARow, length);
    computedBSize = state.ApplyMinMaxBSize(computedBSize);
    state.SetComputedBSize(computedBSize);
  }

  if (usingContainBSize) {
    state.SetComputedBSize(*containBSize);
  }

  ScrollContainerFrame::Reflow(aPresContext, aDesiredSize, state, aStatus);

  mBSizeOfARow = CalcBSizeOfARow();

  if (!mMightNeedSecondPass) {
    NS_ASSERTION(
        !autoBSize || usingContainBSize || BSizeOfARow() == oldBSizeOfARow,
        "How did our BSize of a row change if nothing was dirty?");
    NS_ASSERTION(!autoBSize || usingContainBSize ||
                     !HasAnyStateBits(NS_FRAME_FIRST_REFLOW),
                 "How do we not need a second pass during initial reflow at "
                 "auto BSize?");
    if (!autoBSize || usingContainBSize) {
      // Update our mNumDisplayRows based on our new row block size now
      // that we know it.  Note that if autoBSize and we landed in this
      // code then we already set mNumDisplayRows in CalcIntrinsicBSize.
      //  Also note that we can't use BSizeOfARow() here because that
      // just uses a cached value that we didn't compute.
      nscoord rowBSize = CalcBSizeOfARow();
      if (rowBSize == 0) {
        // Just pick something
        mNumDisplayRows = 1;
      } else {
        mNumDisplayRows = std::max(1, state.ComputedBSize() / rowBSize);
      }
    }

    return;
  }

  mMightNeedSecondPass = false;

  // Now see whether we need a second pass.  If we do, our
  // scrolled frame will have suppressed the scrollbar update.
  if (mBSizeOfARow == oldBSizeOfARow) {
    return;
  }

  // Gotta reflow again.
  // XXXbz We're just changing the block size here; do we need to dirty
  // ourselves or anything like that?  We might need to, per the letter
  // of the reflow protocol, but things seem to work fine without it...
  // Is that just an implementation detail of ScrollContainerFrame that
  // we're depending on?
  ScrollContainerFrame::DidReflow(aPresContext, &state);

  // Now compute the block size we want to have
  nscoord computedBSize = CalcIntrinsicBSize(BSizeOfARow(), length);
  computedBSize = state.ApplyMinMaxBSize(computedBSize);
  state.SetComputedBSize(computedBSize);

  // XXXbz to make the ascent really correct, we should add our
  // mComputedPadding.top to it (and subtract it from descent).  Need that
  // because ScrollContainerFrame just adds in the border....
  aStatus.Reset();
  ScrollContainerFrame::Reflow(aPresContext, aDesiredSize, state, aStatus);
}

uint32_t nsListControlFrame::GetNumberOfRows() {
  return Select().CountRenderedRows();
}

//---------------------------------------------------------
nsresult nsListControlFrame::HandleEvent(nsPresContext* aPresContext,
                                         WidgetGUIEvent* aEvent,
                                         nsEventStatus* aEventStatus) {
  NS_ENSURE_ARG_POINTER(aEventStatus);

  /*const char * desc[] = {"eMouseMove",
                          "NS_MOUSE_LEFT_BUTTON_UP",
                          "NS_MOUSE_LEFT_BUTTON_DOWN",
                          "<NA>","<NA>","<NA>","<NA>","<NA>","<NA>","<NA>",
                          "NS_MOUSE_MIDDLE_BUTTON_UP",
                          "NS_MOUSE_MIDDLE_BUTTON_DOWN",
                          "<NA>","<NA>","<NA>","<NA>","<NA>","<NA>","<NA>","<NA>",
                          "NS_MOUSE_RIGHT_BUTTON_UP",
                          "NS_MOUSE_RIGHT_BUTTON_DOWN",
                          "eMouseOver",
                          "eMouseOut",
                          "NS_MOUSE_LEFT_DOUBLECLICK",
                          "NS_MOUSE_MIDDLE_DOUBLECLICK",
                          "NS_MOUSE_RIGHT_DOUBLECLICK",
                          "NS_MOUSE_LEFT_CLICK",
                          "NS_MOUSE_MIDDLE_CLICK",
                          "NS_MOUSE_RIGHT_CLICK"};
  int inx = aEvent->mMessage - eMouseEventFirst;
  if (inx >= 0 && inx <= (NS_MOUSE_RIGHT_CLICK - eMouseEventFirst)) {
    printf("Mouse in ListFrame %s [%d]\n", desc[inx], aEvent->mMessage);
  } else {
    printf("Mouse in ListFrame <UNKNOWN> [%d]\n", aEvent->mMessage);
  }*/

  if (nsEventStatus_eConsumeNoDefault == *aEventStatus) {
    return NS_OK;
  }

  // disabled state affects how we're selected, but we don't want to go through
  // ScrollContainerFrame if we're disabled.
  if (IsContentDisabled()) {
    return nsIFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
  }

  return ScrollContainerFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
}

HTMLSelectElement& nsListControlFrame::Select() const {
  return *static_cast<HTMLSelectElement*>(GetContent());
}

//---------------------------------------------------------
void nsListControlFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                              nsIFrame* aPrevInFlow) {
  ScrollContainerFrame::Init(aContent, aParent, aPrevInFlow);
}

dom::HTMLOptionElement* nsListControlFrame::GetOption(uint32_t aIndex) const {
  return Select().Item(aIndex);
}

void nsListControlFrame::ElementStateChanged(ElementState aStates) {
  if (aStates.HasState(ElementState::FOCUS)) {
    InvalidateFocus();
  }
}

void nsListControlFrame::GetOptionText(uint32_t aIndex, nsAString& aStr) {
  aStr.Truncate();
  if (dom::HTMLOptionElement* optionElement = GetOption(aIndex)) {
    optionElement->GetRenderedLabel(aStr);
  }
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsListControlFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"ListControl"_ns, aResult);
}
#endif

nscoord nsListControlFrame::GetBSizeOfARow() { return BSizeOfARow(); }

nscoord nsListControlFrame::CalcFallbackRowBSize(float aFontSizeInflation) {
  RefPtr<nsFontMetrics> fontMet =
      nsLayoutUtils::GetFontMetricsForFrame(this, aFontSizeInflation);
  return fontMet->MaxHeight();
}

nscoord nsListControlFrame::CalcIntrinsicBSize(nscoord aBSizeOfARow,
                                               int32_t aNumberOfOptions) {
  if (Style()->StyleUIReset()->mFieldSizing == StyleFieldSizing::Content) {
    int32_t length = GetNumberOfRows();
    return length * aBSizeOfARow;
  }

  mNumDisplayRows = Select().Size();
  if (mNumDisplayRows < 1) {
    mNumDisplayRows = 4;
  }
  return mNumDisplayRows * aBSizeOfARow;
}

void nsListControlFrame::ScrollToFrame(dom::HTMLOptionElement& aOptElement) {
  // otherwise we find the content's frame and scroll to it
  if (nsIFrame* childFrame = aOptElement.GetPrimaryFrame()) {
    RefPtr<mozilla::PresShell> presShell = PresShell();
    presShell->ScrollFrameIntoView(childFrame, Nothing(), AxisScrollParams(),
                                   AxisScrollParams(),
                                   ScrollFlags::ScrollOverflowHidden |
                                       ScrollFlags::ScrollFirstAncestorOnly);
  }
}
