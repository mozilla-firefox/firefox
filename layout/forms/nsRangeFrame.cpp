/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsRangeFrame.h"

#include "ListMutationObserver.h"
#include "gfxContext.h"
#include "mozilla/Assertions.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLDataListElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLOptionElement.h"
#include "nsCSSRendering.h"
#include "nsContentCreatorFunctions.h"
#include "nsDisplayList.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIMutationObserver.h"
#include "nsLayoutUtils.h"
#include "nsNodeInfoManager.h"
#include "nsPresContext.h"
#include "nsTArray.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

// Our intrinsic size is 12em in the main-axis and 1.3em in the cross-axis.
#define MAIN_AXIS_EM_SIZE 12
#define CROSS_AXIS_EM_SIZE 1.3f

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::image;

nsIFrame* NS_NewRangeFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsRangeFrame(aStyle, aPresShell->GetPresContext());
}

nsRangeFrame::nsRangeFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID) {}

void nsRangeFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                        nsIFrame* aPrevInFlow) {
  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);
  if (InputElement().HasAttr(nsGkAtoms::list)) {
    mListMutationObserver = new ListMutationObserver(*this);
  }
}

nsRangeFrame::~nsRangeFrame() = default;

NS_IMPL_FRAMEARENA_HELPERS(nsRangeFrame)

NS_QUERYFRAME_HEAD(nsRangeFrame)
  NS_QUERYFRAME_ENTRY(nsRangeFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

void nsRangeFrame::Destroy(DestroyContext& aContext) {
  NS_ASSERTION(!GetPrevContinuation() && !GetNextContinuation(),
               "nsRangeFrame should not have continuations; if it does we "
               "need to call RegUnregAccessKey only for the first.");

  if (mListMutationObserver) {
    mListMutationObserver->Detach();
  }
  aContext.AddAnonymousContent(mTrackDiv.forget());
  aContext.AddAnonymousContent(mProgressDiv.forget());
  aContext.AddAnonymousContent(mThumbDiv.forget());
  nsContainerFrame::Destroy(aContext);
}

static already_AddRefed<Element> MakeAnonymousDiv(
    Document& aDoc, PseudoStyleType aOldPseudoType,
    PseudoStyleType aModernPseudoType,
    nsTArray<nsIAnonymousContentCreator::ContentInfo>& aElements) {
  RefPtr<Element> result = aDoc.CreateHTMLElement(nsGkAtoms::div);

  // Associate the pseudo-element with the anonymous child.
  if (StaticPrefs::layout_css_modern_range_pseudos_enabled()) {
    result->SetPseudoElementType(aModernPseudoType);
  } else {
    result->SetPseudoElementType(aOldPseudoType);
  }

  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier, or change the return type to void.
  aElements.AppendElement(result);

  return result.forget();
}

nsresult nsRangeFrame::CreateAnonymousContent(
    nsTArray<ContentInfo>& aElements) {
  Document* doc = mContent->OwnerDoc();
  // Create the ::-moz-range-track pseudo-element (a div):
  mTrackDiv = MakeAnonymousDiv(*doc, PseudoStyleType::MozRangeTrack,
                               PseudoStyleType::SliderTrack, aElements);
  // Create the ::-moz-range-progress pseudo-element (a div):
  mProgressDiv = MakeAnonymousDiv(*doc, PseudoStyleType::MozRangeProgress,
                                  PseudoStyleType::SliderFill, aElements);
  // Create the ::-moz-range-thumb pseudo-element (a div):
  mThumbDiv = MakeAnonymousDiv(*doc, PseudoStyleType::MozRangeThumb,
                               PseudoStyleType::SliderThumb, aElements);
  return NS_OK;
}

void nsRangeFrame::AppendAnonymousContentTo(nsTArray<nsIContent*>& aElements,
                                            uint32_t aFilter) {
  if (mTrackDiv) {
    aElements.AppendElement(mTrackDiv);
  }

  if (mProgressDiv) {
    aElements.AppendElement(mProgressDiv);
  }

  if (mThumbDiv) {
    aElements.AppendElement(mThumbDiv);
  }
}

void nsRangeFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                    const nsDisplayListSet& aLists) {
  const nsStyleDisplay* disp = StyleDisplay();
  if (IsThemed(disp)) {
    DisplayBorderBackgroundOutline(aBuilder, aLists);
    // Don't paint our children, but let the thumb be hittable for events.
    if (auto* thumb = mThumbDiv->GetPrimaryFrame();
        thumb && aBuilder->IsForEventDelivery() && !HidesContent()) {
      nsDisplayListSet set(aLists, aLists.Content());
      BuildDisplayListForChild(aBuilder, thumb, set, DisplayChildFlag::Inline);
    }
  } else {
    BuildDisplayListForInline(aBuilder, aLists);
  }
}

void nsRangeFrame::Reflow(nsPresContext* aPresContext,
                          ReflowOutput& aDesiredSize,
                          const ReflowInput& aReflowInput,
                          nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsRangeFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  NS_ASSERTION(mTrackDiv, "::-moz-range-track div must exist!");
  NS_ASSERTION(mProgressDiv, "::-moz-range-progress div must exist!");
  NS_ASSERTION(mThumbDiv, "::-moz-range-thumb div must exist!");
  NS_ASSERTION(!GetPrevContinuation() && !GetNextContinuation(),
               "nsRangeFrame should not have continuations; if it does we "
               "need to call RegUnregAccessKey only for the first.");

  WritingMode wm = aReflowInput.GetWritingMode();
  const auto contentBoxSize = aReflowInput.ComputedSizeWithBSizeFallback([&] {
    return IsInlineOriented() ? AutoCrossSize()
                              : OneEmInAppUnits() * MAIN_AXIS_EM_SIZE;
  });
  aDesiredSize.SetSize(
      wm,
      contentBoxSize + aReflowInput.ComputedLogicalBorderPadding(wm).Size(wm));
  aDesiredSize.SetOverflowAreasToDesiredBounds();

  ReflowChildFrames(aPresContext, aDesiredSize, contentBoxSize, aReflowInput);
  FinishAndStoreOverflow(&aDesiredSize);

  MOZ_ASSERT(aStatus.IsEmpty(), "This type of frame can't be split.");
}

void nsRangeFrame::ReflowChildFrames(nsPresContext* aPresContext,
                                     ReflowOutput& aDesiredSize,
                                     const LogicalSize& aContentBoxSize,
                                     const ReflowInput& aReflowInput) {
  const auto parentWM = aReflowInput.GetWritingMode();
  // The width/height of our content box, which is the available width/height
  // for our anonymous content.
  const nsSize rangeFrameContentBoxSize =
      aContentBoxSize.GetPhysicalSize(parentWM);
  for (auto* child : mFrames) {
    auto* content = child->GetContent();
    const WritingMode wm = child->GetWritingMode();
    const LogicalSize parentSizeInChildWM =
        aContentBoxSize.ConvertTo(wm, parentWM);
    LogicalSize availSize = parentSizeInChildWM;
    availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;
    ReflowInput childReflowInput(aPresContext, aReflowInput, child, availSize,
                                 Some(parentSizeInChildWM));

    const nsPoint pos = [&] {
      if (content != mTrackDiv) {
        // Where we position the thumb and range-progress depends on its size,
        // so we first reflow them at {0,0} to obtain the size, then position
        // them afterwards.
        return nsPoint();
      }
      // Find the x/y position of the track. The idea here is that we allow
      // content authors to style the width, height, border and padding of the
      // track, but we ignore margin and positioning properties and do the
      // positioning ourself to keep the center of the track's border box on the
      // center of the nsRangeFrame's content. These coordinates are with
      // respect to the nsRangeFrame's border-box.
      nscoord trackX = rangeFrameContentBoxSize.Width() / 2;
      nscoord trackY = rangeFrameContentBoxSize.Height() / 2;

      // Account for the track's border and padding (we ignore its margin):
      // FIXME(emilio): Assumes the track height is constrained, which might not
      // be true if authors override it.
      trackX -= childReflowInput.ComputedPhysicalBorderPadding().left +
                childReflowInput.ComputedWidth() / 2;
      trackY -= childReflowInput.ComputedPhysicalBorderPadding().top +
                childReflowInput.ComputedHeight() / 2;

      // Make relative to our border box instead of our content box:
      trackX += aReflowInput.ComputedPhysicalBorderPadding().left;
      trackY += aReflowInput.ComputedPhysicalBorderPadding().top;
      return nsPoint(trackX, trackY);
    }();

    nsReflowStatus frameStatus;
    ReflowOutput childDesiredSize(aReflowInput);
    ReflowChild(child, aPresContext, childDesiredSize, childReflowInput, pos.x,
                pos.y, ReflowChildFlags::Default, frameStatus);
    MOZ_ASSERT(
        frameStatus.IsFullyComplete(),
        "We gave our child unconstrained height, so it should be complete");
    FinishReflowChild(child, aPresContext, childDesiredSize, &childReflowInput,
                      pos.x, pos.y, ReflowChildFlags::Default);
    if (content == mThumbDiv) {
      DoUpdateThumbPosition(child, rangeFrameContentBoxSize);
    } else if (content == mProgressDiv) {
      DoUpdateRangeProgressFrame(child, rangeFrameContentBoxSize);
    }
    ConsiderChildOverflow(aDesiredSize.mOverflowAreas, child);
  }
}

#ifdef ACCESSIBILITY
a11y::AccType nsRangeFrame::AccessibleType() { return a11y::eHTMLRangeType; }
#endif

double nsRangeFrame::GetValueAsFractionOfRange() {
  const auto& input = InputElement();
  if (MOZ_UNLIKELY(!input.IsDoneCreating())) {
    // Our element isn't done being created, so its values haven't yet been
    // sanitized! (It's rare that we'd be reflowed when our element is in this
    // state, but it can happen if the parser decides to yield while processing
    // its tasks to build the element.)  We can't trust that any of our numeric
    // values will make sense until they've been sanitized; so for now, just
    // use 0.0 as a fallback fraction-of-range value here (i.e. behave as if
    // we're at our minimum, which is how the spec handles some edge cases).
    return 0.0;
  }
  return GetDoubleAsFractionOfRange(input.GetValueAsDecimal());
}

double nsRangeFrame::GetDoubleAsFractionOfRange(const Decimal& aValue) {
  auto& input = InputElement();

  Decimal minimum = input.GetMinimum();
  Decimal maximum = input.GetMaximum();

  MOZ_ASSERT(aValue.isFinite() && minimum.isFinite() && maximum.isFinite(),
             "type=range should have a default maximum/minimum");

  if (maximum <= minimum) {
    // Avoid rounding triggering the assert by checking against an epsilon.
    MOZ_ASSERT((aValue - minimum).abs().toDouble() <
                   std::numeric_limits<float>::epsilon(),
               "Unsanitized value");
    return 0.0;
  }

  MOZ_ASSERT(aValue >= minimum && aValue <= maximum, "Unsanitized value");

  return ((aValue - minimum) / (maximum - minimum)).toDouble();
}

Decimal nsRangeFrame::GetValueAtEventPoint(WidgetGUIEvent* aEvent) {
  MOZ_ASSERT(
      aEvent->mClass == eMouseEventClass || aEvent->mClass == eTouchEventClass,
      "Unexpected event type - aEvent->mRefPoint may be meaningless");

  MOZ_ASSERT(mContent->IsHTMLElement(nsGkAtoms::input), "bad cast");
  dom::HTMLInputElement* input =
      static_cast<dom::HTMLInputElement*>(GetContent());

  MOZ_ASSERT(input->ControlType() == FormControlType::InputRange);

  Decimal minimum = input->GetMinimum();
  Decimal maximum = input->GetMaximum();
  MOZ_ASSERT(minimum.isFinite() && maximum.isFinite(),
             "type=range should have a default maximum/minimum");
  if (maximum <= minimum) {
    return minimum;
  }
  Decimal range = maximum - minimum;

  LayoutDeviceIntPoint absPoint;
  if (aEvent->mClass == eTouchEventClass) {
    MOZ_ASSERT(aEvent->AsTouchEvent()->mTouches.Length() == 1,
               "Unexpected number of mTouches");
    absPoint = aEvent->AsTouchEvent()->mTouches[0]->mRefPoint;
  } else {
    absPoint = aEvent->mRefPoint;
  }
  nsPoint point = nsLayoutUtils::GetEventCoordinatesRelativeTo(
      aEvent, absPoint, RelativeTo{this});

  if (point == nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE)) {
    // We don't want to change the current value for this error state.
    return static_cast<dom::HTMLInputElement*>(GetContent())
        ->GetValueAsDecimal();
  }

  nsRect rangeRect;
  nsSize thumbSize;
  if (IsThemed()) {
    // Themed ranges draw on the border-box rect.
    rangeRect = GetRectRelativeToSelf();
    // We need to get the size of the thumb from the theme.
    nscoord min = CSSPixel::ToAppUnits(
        PresContext()->Theme()->GetMinimumRangeThumbSize());
    MOZ_ASSERT(min, "The thumb is expected to take up some slider space");
    thumbSize = nsSize(min, min);
  } else {
    rangeRect = GetContentRectRelativeToSelf();
    nsIFrame* thumbFrame = mThumbDiv->GetPrimaryFrame();
    if (thumbFrame) {  // diplay:none?
      thumbSize = thumbFrame->GetSize();
    }
  }

  Decimal fraction;
  if (IsHorizontal()) {
    nscoord traversableDistance = rangeRect.width - thumbSize.width;
    if (traversableDistance <= 0) {
      return minimum;
    }
    nscoord posAtStart = rangeRect.x + thumbSize.width / 2;
    nscoord posAtEnd = posAtStart + traversableDistance;
    nscoord posOfPoint = std::clamp(point.x, posAtStart, posAtEnd);
    fraction = Decimal(posOfPoint - posAtStart) / Decimal(traversableDistance);
    if (IsRightToLeft()) {
      fraction = Decimal(1) - fraction;
    }
  } else {
    nscoord traversableDistance = rangeRect.height - thumbSize.height;
    if (traversableDistance <= 0) {
      return minimum;
    }
    nscoord posAtStart = rangeRect.y + thumbSize.height / 2;
    nscoord posAtEnd = posAtStart + traversableDistance;
    nscoord posOfPoint = std::clamp(point.y, posAtStart, posAtEnd);
    // For a vertical range, the top (posAtStart) is the highest value, so we
    // subtract the fraction from 1.0 to get that polarity correct.
    fraction = Decimal(posOfPoint - posAtStart) / Decimal(traversableDistance);
    if (IsUpwards()) {
      fraction = Decimal(1) - fraction;
    }
  }

  MOZ_ASSERT(fraction >= Decimal(0) && fraction <= Decimal(1));
  return minimum + fraction * range;
}

void nsRangeFrame::UpdateForValueChange() {
  if (IsSubtreeDirty()) {
    return;  // we're going to be updated when we reflow
  }
  nsIFrame* rangeProgressFrame = mProgressDiv->GetPrimaryFrame();
  nsIFrame* thumbFrame = mThumbDiv->GetPrimaryFrame();
  if (!rangeProgressFrame && !thumbFrame) {
    return;  // diplay:none?
  }
  const nsSize contentBoxSize = GetContentRect().Size();
  if (rangeProgressFrame) {
    DoUpdateRangeProgressFrame(rangeProgressFrame, contentBoxSize);
  }
  if (thumbFrame) {
    DoUpdateThumbPosition(thumbFrame, contentBoxSize);
  }
  if (IsThemed()) {
    // We don't know the exact dimensions or location of the thumb when native
    // theming is applied, so we just repaint the entire range.
    InvalidateFrame();
  }

#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->RangeValueChanged(PresShell(), mContent);
  }
#endif

  SchedulePaint();
}

nsTArray<Decimal> nsRangeFrame::TickMarks() {
  nsTArray<Decimal> tickMarks;
  auto& input = InputElement();
  auto* list = input.GetListInternal();
  if (!list) {
    return tickMarks;
  }
  auto min = input.GetMinimum();
  auto max = input.GetMaximum();
  for (nsINode* n = list->GetFirstChild(); n; n = n->GetNextNode(list)) {
    auto* option = HTMLOptionElement::FromNode(n);
    if (!option || option->Disabled()) {
      continue;
    }
    nsAutoString str;
    option->GetValue(str);
    auto tickMark = HTMLInputElement::StringToDecimal(str);
    if (tickMark.isNaN() || tickMark < min || tickMark > max ||
        input.ValueIsStepMismatch(tickMark)) {
      continue;
    }
    tickMarks.AppendElement(tickMark);
  }
  tickMarks.Sort();
  return tickMarks;
}

Decimal nsRangeFrame::NearestTickMark(const Decimal& aValue) {
  auto tickMarks = TickMarks();
  if (tickMarks.IsEmpty() || aValue.isNaN()) {
    return Decimal::nan();
  }
  size_t index;
  if (BinarySearch(tickMarks, 0, tickMarks.Length(), aValue, &index)) {
    return tickMarks[index];
  }
  if (index == tickMarks.Length()) {
    return tickMarks.LastElement();
  }
  if (index == 0) {
    return tickMarks[0];
  }
  const auto& smallerTickMark = tickMarks[index - 1];
  const auto& largerTickMark = tickMarks[index];
  MOZ_ASSERT(smallerTickMark < aValue);
  MOZ_ASSERT(largerTickMark > aValue);
  return (aValue - smallerTickMark).abs() < (aValue - largerTickMark).abs()
             ? smallerTickMark
             : largerTickMark;
}

mozilla::dom::HTMLInputElement& nsRangeFrame::InputElement() const {
  MOZ_ASSERT(mContent->IsHTMLElement(nsGkAtoms::input), "bad cast");
  auto& input = *static_cast<dom::HTMLInputElement*>(GetContent());
  MOZ_ASSERT(input.ControlType() == FormControlType::InputRange);
  return input;
}

void nsRangeFrame::DoUpdateThumbPosition(nsIFrame* aThumbFrame,
                                         const nsSize& aRangeContentBoxSize) {
  MOZ_ASSERT(aThumbFrame);

  // The idea here is that we want to position the thumb so that the center
  // of the thumb is on an imaginary line drawn from the middle of one edge
  // of the range frame's content box to the middle of the opposite edge of
  // its content box (the opposite edges being the left/right edge if the
  // range is horizontal, or else the top/bottom edges if the range is
  // vertical). How far along this line the center of the thumb is placed
  // depends on the value of the range.

  nsMargin borderAndPadding = GetUsedBorderAndPadding();
  nsPoint newPosition(borderAndPadding.left, borderAndPadding.top);

  nsSize thumbSize = aThumbFrame->GetSize();
  double fraction = GetValueAsFractionOfRange();
  MOZ_ASSERT(fraction >= 0.0 && fraction <= 1.0);

  if (IsHorizontal()) {
    if (thumbSize.width < aRangeContentBoxSize.width) {
      nscoord traversableDistance =
          aRangeContentBoxSize.width - thumbSize.width;
      if (IsRightToLeft()) {
        newPosition.x += NSToCoordRound((1.0 - fraction) * traversableDistance);
      } else {
        newPosition.x += NSToCoordRound(fraction * traversableDistance);
      }
      newPosition.y += (aRangeContentBoxSize.height - thumbSize.height) / 2;
    }
  } else {
    if (thumbSize.height < aRangeContentBoxSize.height) {
      nscoord traversableDistance =
          aRangeContentBoxSize.height - thumbSize.height;
      newPosition.x += (aRangeContentBoxSize.width - thumbSize.width) / 2;
      if (IsUpwards()) {
        newPosition.y += NSToCoordRound((1.0 - fraction) * traversableDistance);
      } else {
        newPosition.y += NSToCoordRound(fraction * traversableDistance);
      }
    }
  }
  aThumbFrame->SetPosition(newPosition);
}

void nsRangeFrame::DoUpdateRangeProgressFrame(
    nsIFrame* aProgressFrame, const nsSize& aRangeContentBoxSize) {
  MOZ_ASSERT(aProgressFrame);

  // The idea here is that we want to position the ::-moz-range-progress
  // pseudo-element so that the center line running along its length is on the
  // corresponding center line of the nsRangeFrame's content box. In the other
  // dimension, we align the "start" edge of the ::-moz-range-progress
  // pseudo-element's border-box with the corresponding edge of the
  // nsRangeFrame's content box, and we size the progress element's border-box
  // to have a length of GetValueAsFractionOfRange() times the nsRangeFrame's
  // content-box size.
  nsMargin borderAndPadding = GetUsedBorderAndPadding();
  nsSize progSize = aProgressFrame->GetSize();
  nsRect progRect(borderAndPadding.left, borderAndPadding.top, progSize.width,
                  progSize.height);

  double fraction = GetValueAsFractionOfRange();
  MOZ_ASSERT(fraction >= 0.0 && fraction <= 1.0);

  if (IsHorizontal()) {
    nscoord progLength = NSToCoordRound(fraction * aRangeContentBoxSize.width);
    if (IsRightToLeft()) {
      progRect.x += aRangeContentBoxSize.width - progLength;
    }
    progRect.y += (aRangeContentBoxSize.height - progSize.height) / 2;
    progRect.width = progLength;
  } else {
    nscoord progLength = NSToCoordRound(fraction * aRangeContentBoxSize.height);
    progRect.x += (aRangeContentBoxSize.width - progSize.width) / 2;
    if (IsUpwards()) {
      progRect.y += aRangeContentBoxSize.height - progLength;
    }
    progRect.height = progLength;
  }
  aProgressFrame->SetRect(progRect);
}

nsresult nsRangeFrame::AttributeChanged(int32_t aNameSpaceID,
                                        nsAtom* aAttribute,
                                        AttrModType aModType) {
  NS_ASSERTION(mTrackDiv, "The track div must exist!");
  NS_ASSERTION(mThumbDiv, "The thumb div must exist!");

  if (aNameSpaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::value || aAttribute == nsGkAtoms::min ||
        aAttribute == nsGkAtoms::max || aAttribute == nsGkAtoms::step) {
      // We want to update the position of the thumb, except in one special
      // case: If the value attribute is being set, it is possible that we are
      // in the middle of a type change away from type=range, under the
      // SetAttr(..., nsGkAtoms::value, ...) call in HTMLInputElement::
      // HandleTypeChange. In that case the HTMLInputElement's type will
      // already have changed, and if we call UpdateForValueChange()
      // we'll fail the asserts under that call that check the type of our
      // HTMLInputElement. Given that we're changing away from being a range
      // and this frame will shortly be destroyed, there's no point in calling
      // UpdateForValueChange() anyway.
      MOZ_ASSERT(mContent->IsHTMLElement(nsGkAtoms::input), "bad cast");
      bool typeIsRange =
          static_cast<dom::HTMLInputElement*>(GetContent())->ControlType() ==
          FormControlType::InputRange;
      // If script changed the <input>'s type before setting these attributes
      // then we don't need to do anything since we are going to be reframed.
      if (typeIsRange) {
        UpdateForValueChange();
      }
    } else if (aAttribute == nsGkAtoms::orient) {
      PresShell()->FrameNeedsReflow(this, IntrinsicDirty::None,
                                    NS_FRAME_IS_DIRTY);
    } else if (aAttribute == nsGkAtoms::list) {
      const bool isRemoval = aModType == AttrModType::Removal;
      if (mListMutationObserver) {
        mListMutationObserver->Detach();
        if (isRemoval) {
          mListMutationObserver = nullptr;
        } else {
          mListMutationObserver->Attach();
        }
      } else if (!isRemoval) {
        mListMutationObserver = new ListMutationObserver(*this, true);
      }
    }
  }

  return nsContainerFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

nscoord nsRangeFrame::AutoCrossSize() {
  nscoord minCrossSize =
      IsThemed() ? CSSPixel::ToAppUnits(
                       PresContext()->Theme()->GetMinimumRangeThumbSize())
                 : 0;
  return std::max(minCrossSize,
                  NSToCoordRound(OneEmInAppUnits() * CROSS_AXIS_EM_SIZE));
}

nscoord nsRangeFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                     IntrinsicISizeType aType) {
  if (aType == IntrinsicISizeType::MinISize) {
    const auto* pos = StylePosition();
    auto wm = GetWritingMode();
    const auto iSize = pos->ISize(wm, AnchorPosResolutionParams::From(this));
    if (iSize->HasPercent()) {
      // https://drafts.csswg.org/css-sizing-3/#percentage-sizing
      // https://drafts.csswg.org/css-sizing-3/#min-content-zero
      return nsLayoutUtils::ResolveToLength<true>(iSize->AsLengthPercentage(),
                                                  nscoord(0));
    }
  }
  if (IsInlineOriented()) {
    return OneEmInAppUnits() * MAIN_AXIS_EM_SIZE;
  }
  return AutoCrossSize();
}

bool nsRangeFrame::IsHorizontal() const {
  dom::HTMLInputElement* element =
      static_cast<dom::HTMLInputElement*>(GetContent());
  return element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::orient,
                              nsGkAtoms::horizontal, eCaseMatters) ||
         (!element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::orient,
                                nsGkAtoms::vertical, eCaseMatters) &&
          GetWritingMode().IsVertical() ==
              element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::orient,
                                   nsGkAtoms::block, eCaseMatters));
}

double nsRangeFrame::GetMin() const {
  return static_cast<dom::HTMLInputElement*>(GetContent())
      ->GetMinimum()
      .toDouble();
}

double nsRangeFrame::GetMax() const {
  return static_cast<dom::HTMLInputElement*>(GetContent())
      ->GetMaximum()
      .toDouble();
}

double nsRangeFrame::GetValue() const {
  return static_cast<dom::HTMLInputElement*>(GetContent())
      ->GetValueAsDecimal()
      .toDouble();
}

bool nsRangeFrame::ShouldUseNativeStyle() const {
  nsIFrame* trackFrame = mTrackDiv->GetPrimaryFrame();
  nsIFrame* progressFrame = mProgressDiv->GetPrimaryFrame();
  nsIFrame* thumbFrame = mThumbDiv->GetPrimaryFrame();

  return StyleDisplay()->EffectiveAppearance() == StyleAppearance::Range &&
         trackFrame &&
         !trackFrame->Style()->HasAuthorSpecifiedBorderOrBackground() &&
         progressFrame &&
         !progressFrame->Style()->HasAuthorSpecifiedBorderOrBackground() &&
         thumbFrame &&
         !thumbFrame->Style()->HasAuthorSpecifiedBorderOrBackground();
}
