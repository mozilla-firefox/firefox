/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * rendering object for the point that anchors out-of-flow rendering
 * objects such as floats and absolutely positioned elements
 */

#include "nsPlaceholderFrame.h"

#include "gfxContext.h"
#include "gfxUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/ServoStyleSetInlines.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/gfx/2D.h"
#include "nsCSSFrameConstructor.h"
#include "nsDisplayList.h"
#include "nsIContentInlines.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;

nsPlaceholderFrame* NS_NewPlaceholderFrame(PresShell* aPresShell,
                                           ComputedStyle* aStyle,
                                           nsFrameState aTypeBits) {
  return new nsPlaceholderFrame(aStyle, aPresShell->GetPresContext(),
                                aTypeBits);
}

NS_IMPL_FRAMEARENA_HELPERS(nsPlaceholderFrame)

#ifdef DEBUG
NS_QUERYFRAME_HEAD(nsPlaceholderFrame)
  NS_QUERYFRAME_ENTRY(nsPlaceholderFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsIFrame)
#endif

/* virtual */
void nsPlaceholderFrame::AddInlineMinISize(const IntrinsicSizeInput& aInput,
                                           InlineMinISizeData* aData) {
  // Override AddInlineMinISize so that *nothing* happens. In
  // particular, we don't want to zero out |aData->mTrailingWhitespace|,
  // since nsLineLayout skips placeholders when trimming trailing
  // whitespace, and we don't want to set aData->mSkipWhitespace to
  // false.

  // ...but push floats onto aData's list.
  AddFloatToIntrinsicISizeData(aInput, IntrinsicISizeType::MinISize, aData);
}

/* virtual */
void nsPlaceholderFrame::AddInlinePrefISize(const IntrinsicSizeInput& aInput,
                                            InlinePrefISizeData* aData) {
  // Override AddInlinePrefISize so that *nothing* happens. In
  // particular, we don't want to zero out |aData->mTrailingWhitespace|,
  // since nsLineLayout skips placeholders when trimming trailing
  // whitespace, and we don't want to set aData->mSkipWhitespace to
  // false.

  // ...but push floats onto aData's list.
  AddFloatToIntrinsicISizeData(aInput, IntrinsicISizeType::PrefISize, aData);
}

void nsPlaceholderFrame::AddFloatToIntrinsicISizeData(
    const IntrinsicSizeInput& aInput, IntrinsicISizeType aType,
    InlineIntrinsicISizeData* aData) const {
  if (mOutOfFlowFrame->IsFloating()) {
    const IntrinsicSizeInput floatInput(
        aInput, mOutOfFlowFrame->GetWritingMode(), GetWritingMode());
    const nscoord floatISize = nsLayoutUtils::IntrinsicForContainer(
        floatInput.mContext, mOutOfFlowFrame, aType,
        floatInput.mPercentageBasisForChildren);
    aData->mFloats.EmplaceBack(mOutOfFlowFrame, floatISize);
  }
}

void nsPlaceholderFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aDesiredSize,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  // NOTE that the ReflowInput passed to this method is not fully initialized,
  // on the grounds that reflowing a placeholder is a rather trivial operation.
  // (See bug 1367711.)

#ifdef DEBUG
  // We should be getting reflowed before our out-of-flow. If this is our first
  // reflow, and our out-of-flow has already received its first reflow (before
  // us), complain.
  //
  // Popups are an exception though, because their position doesn't depend on
  // the placeholder, so they don't have this requirement (and this condition
  // doesn't hold anyways because the default popupgroup goes before than the
  // default tooltip, for example). Same for the backdrop.
  // TODO(emilio): All top layer nodes technically can hit this, but their
  // static pos is supposed to be 0, 0, see
  // https://github.com/w3c/csswg-drafts/issues/9939.
  //
  // We also have an exception if the out-of-flow created an orthogonal flow,
  // because in this case we may have needed to do a measuring reflow during
  // intrinsic size computation. That's OK because it does not depend on the
  // placeholder being reflowed first.
  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW) &&
      !mOutOfFlowFrame->IsMenuPopupFrame() &&
      mOutOfFlowFrame->Style()->GetPseudoType() != PseudoStyleType::Backdrop &&
      !mOutOfFlowFrame->HasAnyStateBits(NS_FRAME_FIRST_REFLOW) &&
      !mOutOfFlowFrame->GetWritingMode().IsOrthogonalTo(GetWritingMode())) {
    // Unfortunately, this can currently happen when the placeholder is in a
    // later continuation or later IB-split sibling than its out-of-flow (as
    // is the case in some of our existing unit tests). So for now, in that
    // case, we'll warn instead of asserting.
    bool isInContinuationOrIBSplit = false;
    nsIFrame* ancestor = this;
    while ((ancestor = ancestor->GetParent())) {
      if (nsLayoutUtils::GetPrevContinuationOrIBSplitSibling(ancestor)) {
        isInContinuationOrIBSplit = true;
        break;
      }
    }

    if (isInContinuationOrIBSplit) {
      NS_WARNING("Out-of-flow frame got reflowed before its placeholder");
    } else {
      NS_ERROR("Out-of-flow frame got reflowed before its placeholder");
    }
  }
#endif

  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsPlaceholderFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  aDesiredSize.ClearSize();
}

static FrameChildListID ChildListIDForOutOfFlow(nsFrameState aPlaceholderState,
                                                const nsIFrame* aChild) {
  if (aPlaceholderState & PLACEHOLDER_FOR_FLOAT) {
    return FrameChildListID::Float;
  }
  MOZ_ASSERT(aPlaceholderState &
             (PLACEHOLDER_FOR_FIXEDPOS | PLACEHOLDER_FOR_ABSPOS));
  return FrameChildListID::Absolute;
}

void nsPlaceholderFrame::Destroy(DestroyContext& aContext) {
  if (nsIFrame* oof = mOutOfFlowFrame) {
    mOutOfFlowFrame = nullptr;
    oof->RemoveProperty(nsIFrame::PlaceholderFrameProperty());

    // Destroy the out of flow now.
    ChildListID listId = ChildListIDForOutOfFlow(GetStateBits(), oof);
    nsFrameManager* fm = PresContext()->FrameConstructor();
    fm->RemoveFrame(aContext, listId, oof);
  }

  nsIFrame::Destroy(aContext);
}

/* virtual */
bool nsPlaceholderFrame::CanContinueTextRun() const {
  if (!mOutOfFlowFrame) {
    return false;
  }
  // first-letter frames can continue text runs, and placeholders for floated
  // first-letter frames can too
  return mOutOfFlowFrame->CanContinueTextRun();
}

ComputedStyle* nsPlaceholderFrame::GetParentComputedStyleForOutOfFlow(
    nsIFrame** aProviderFrame) const {
  MOZ_ASSERT(GetParent(), "How can we not have a parent here?");

  Element* parentElement =
      mContent ? mContent->GetFlattenedTreeParentElement() : nullptr;
  // See the similar code in nsIFrame::DoGetParentComputedStyle.
  if (parentElement && MOZ_LIKELY(parentElement->HasServoData()) &&
      Servo_Element_IsDisplayContents(parentElement)) {
    RefPtr<ComputedStyle> style =
        ServoStyleSet::ResolveServoStyle(*parentElement);
    *aProviderFrame = nullptr;
    // See the comment in GetParentComputedStyle to see why returning this as a
    // weak ref is fine.
    return style;
  }

  return GetLayoutParentStyleForOutOfFlow(aProviderFrame);
}

ComputedStyle* nsPlaceholderFrame::GetLayoutParentStyleForOutOfFlow(
    nsIFrame** aProviderFrame) const {
  // Lie about our pseudo so we can step out of all anon boxes and
  // pseudo-elements.  The other option would be to reimplement the
  // {ib} split gunk here.
  //
  // See the hack in CorrectStyleParentFrame for why we pass `MAX`.
  *aProviderFrame = CorrectStyleParentFrame(GetParent(), PseudoStyleType::MAX);
  return *aProviderFrame ? (*aProviderFrame)->Style() : nullptr;
}

#if defined(DEBUG) || (defined(MOZ_REFLOW_PERF_DSP) && defined(MOZ_REFLOW_PERF))

void nsPlaceholderFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                          const nsDisplayListSet& aLists) {
  DO_GLOBAL_REFLOW_COUNT_DSP("nsPlaceholderFrame");
}
#endif  // DEBUG || (MOZ_REFLOW_PERF_DSP && MOZ_REFLOW_PERF)

#ifdef DEBUG_FRAME_DUMP
nsresult nsPlaceholderFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"Placeholder"_ns, aResult);
}

void nsPlaceholderFrame::List(FILE* out, const char* aPrefix,
                              ListFlags aFlags) const {
  nsCString str;
  ListGeneric(str, aPrefix, aFlags);

  if (mOutOfFlowFrame) {
    str += " outOfFlowFrame=";
    str += mOutOfFlowFrame->ListTag(
        aFlags.contains(ListFlag::OnlyListDeterministicInfo));
  }
  fprintf_stderr(out, "%s\n", str.get());
}
#endif
