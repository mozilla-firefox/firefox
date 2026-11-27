/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for HTML <br> elements */

#include "gfxContext.h"
#include "mozilla/CaretAssociationHint.h"
#include "mozilla/Likely.h"
#include "mozilla/PresShell.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/dom/HTMLBRElement.h"
#include "nsBlockFrame.h"
#include "nsCOMPtr.h"
#include "nsComputedDOMStyle.h"
#include "nsContainerFrame.h"
#include "nsFontMetrics.h"
#include "nsHTMLParts.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsLineLayout.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"
#include "nsTextFrame.h"

// FOR SELECTION
#include "nsIContent.h"
// END INCLUDES FOR SELECTION

using namespace mozilla;

namespace mozilla {

class BRFrame final : public nsIFrame {
 public:
  NS_DECL_FRAMEARENA_HELPERS(BRFrame)

  friend nsIFrame* ::NS_NewBRFrame(mozilla::PresShell* aPresShell,
                                   ComputedStyle* aStyle);

  ContentOffsets CalcContentOffsetsFromFramePoint(
      const nsPoint& aPoint) override;

  FrameSearchResult PeekOffsetNoAmount(bool aForward,
                                       int32_t* aOffset) override;
  FrameSearchResult PeekOffsetCharacter(
      bool aForward, int32_t* aOffset,
      PeekOffsetCharacterOptions aOptions =
          PeekOffsetCharacterOptions()) override;
  FrameSearchResult PeekOffsetWord(bool aForward, bool aWordSelectEatSpace,
                                   bool aIsKeyboardSelect, int32_t* aOffset,
                                   PeekWordState* aState,
                                   bool aTrimSpaces) override;

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aMetrics,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;
  void AddInlineMinISize(const IntrinsicSizeInput& aInput,
                         InlineMinISizeData* aData) override;
  void AddInlinePrefISize(const IntrinsicSizeInput& aInput,
                          InlinePrefISizeData* aData) override;

  Maybe<nscoord> GetNaturalBaselineBOffset(
      WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext) const override;

#ifdef ACCESSIBILITY
  mozilla::a11y::AccType AccessibleType() override;
#endif

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"BR"_ns, aResult);
  }
#endif

 protected:
  BRFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : nsIFrame(aStyle, aPresContext, kClassID),
        mAscent(NS_INTRINSIC_ISIZE_UNKNOWN) {}

  virtual ~BRFrame();

  nscoord mAscent;
};

}  // namespace mozilla

nsIFrame* NS_NewBRFrame(mozilla::PresShell* aPresShell, ComputedStyle* aStyle) {
  return new BRFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(BRFrame)

BRFrame::~BRFrame() = default;

void BRFrame::Reflow(nsPresContext* aPresContext, ReflowOutput& aMetrics,
                     const ReflowInput& aReflowInput, nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("BRFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  WritingMode wm = aReflowInput.GetWritingMode();
  LogicalSize finalSize(wm);
  finalSize.BSize(wm) = 0;  // BR frames with block size 0 are ignored in quirks
                            // mode by nsLineLayout::VerticalAlignFrames .
                            // However, it's not always 0.  See below.
  finalSize.ISize(wm) = 0;
  aMetrics.SetBlockStartAscent(0);

  // Only when the BR is operating in a line-layout situation will it
  // behave like a BR. Additionally, we suppress breaks from BR inside
  // of ruby frames. To determine if we're inside ruby, we have to rely
  // on the *parent's* ShouldSuppressLineBreak() method, instead of our
  // own, because we may have custom "display" value that makes our
  // ShouldSuppressLineBreak() return false.
  nsLineLayout* ll = aReflowInput.mLineLayout;
  if (ll && !GetParent()->Style()->ShouldSuppressLineBreak()) {
    // Note that the compatibility mode check excludes AlmostStandards
    // mode, since this is the inline box model.  See bug 161691.
    if (ll->LineIsEmpty() ||
        aPresContext->CompatibilityMode() == eCompatibility_FullStandards) {
      // The line is logically empty; any whitespace is trimmed away.
      //
      // If this frame is going to terminate the line we know
      // that nothing else will go on the line. Therefore, in this
      // case, we provide some height for the BR frame so that it
      // creates some vertical whitespace.  It's necessary to use the
      // line-height rather than the font size because the
      // quirks-mode fix that doesn't apply the block's min
      // line-height makes this necessary to make BR cause a line
      // of the full line-height

      // We also do this in strict mode because BR should act like a
      // normal inline frame.  That line-height is used is important
      // here for cases where the line-height is less than 1.
      RefPtr<nsFontMetrics> fm =
          nsLayoutUtils::GetInflatedFontMetricsForFrame(GetParent());
      if (fm) {
        nscoord logicalHeight;
        if (MOZ_LIKELY(aReflowInput.mParentReflowInput)) {
          logicalHeight = aReflowInput.mParentReflowInput->GetLineHeight();
        } else {
          logicalHeight = aReflowInput.GetLineHeight();
        }
        finalSize.BSize(wm) = logicalHeight;
        aMetrics.SetBlockStartAscent(nsLayoutUtils::GetCenteredFontBaseline(
            fm, logicalHeight, wm.IsLineInverted()));
      } else {
        aMetrics.SetBlockStartAscent(aMetrics.BSize(wm) = 0);
      }
    }

    // Return our reflow status
    aStatus.SetInlineLineBreakAfter(
        aReflowInput.mStyleDisplay->UsedClear(aReflowInput.GetCBWritingMode()));
    ll->SetLineEndsInBR(true);
  }

  aMetrics.SetSize(wm, finalSize);
  aMetrics.SetOverflowAreasToDesiredBounds();

  mAscent = aMetrics.BlockStartAscent();
}

/* virtual */
void BRFrame::AddInlineMinISize(const IntrinsicSizeInput& aInput,
                                InlineMinISizeData* aData) {
  if (!GetParent()->Style()->ShouldSuppressLineBreak()) {
    aData->ForceBreak();
  }
}

/* virtual */
void BRFrame::AddInlinePrefISize(const IntrinsicSizeInput& aInput,
                                 InlinePrefISizeData* aData) {
  if (!GetParent()->Style()->ShouldSuppressLineBreak()) {
    aData->ForceBreak();
  }
}

Maybe<nscoord> BRFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  if (aBaselineGroup == BaselineSharingGroup::Last) {
    return Nothing{};
  }
  return Some(mAscent);
}

nsIFrame::ContentOffsets BRFrame::CalcContentOffsetsFromFramePoint(
    const nsPoint& aPoint) {
  ContentOffsets offsets;
  offsets.content = mContent->GetParent();
  if (offsets.content) {
    offsets.offset = offsets.content->ComputeIndexOf_Deprecated(mContent);
    offsets.secondaryOffset = offsets.offset;
    offsets.associate = CaretAssociationHint::After;
  }
  return offsets;
}

nsIFrame::FrameSearchResult BRFrame::PeekOffsetNoAmount(bool aForward,
                                                        int32_t* aOffset) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  int32_t startOffset = *aOffset;
  // If we hit the end of a BR going backwards, go to its beginning and stay
  // there.
  if (!aForward && startOffset != 0) {
    *aOffset = 0;
    return FOUND;
  }
  // Otherwise, stop if we hit the beginning, continue (forward) if we hit the
  // end.
  return (startOffset == 0) ? FOUND : CONTINUE;
}

nsIFrame::FrameSearchResult BRFrame::PeekOffsetCharacter(
    bool aForward, int32_t* aOffset, PeekOffsetCharacterOptions aOptions) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  // Keep going. The actual line jumping will stop us.
  return CONTINUE;
}

nsIFrame::FrameSearchResult BRFrame::PeekOffsetWord(
    bool aForward, bool aWordSelectEatSpace, bool aIsKeyboardSelect,
    int32_t* aOffset, PeekWordState* aState, bool aTrimSpaces) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  // Keep going. The actual line jumping will stop us.
  return CONTINUE;
}

#ifdef ACCESSIBILITY
a11y::AccType BRFrame::AccessibleType() {
  dom::HTMLBRElement* brElement = dom::HTMLBRElement::FromNode(mContent);

  if (!brElement->IsPaddingForEmptyLastLine()) {
    // Even if this <br> is a "padding <br> element" used when there is no text
    // in an editor, it may be surrounded by before/after pseudo element
    // content. Therefore, we need to treat it as a normal <br>.
    return a11y::eHTMLBRType;
  }

  // If it's a padding <br> element used in the anonymous subtree of <textarea>,
  // we don't need to expose it as a line break because of in an replaced
  // content.
  if (brElement->IsInNativeAnonymousSubtree()) {
    const auto* textControlElement = TextControlElement::FromNodeOrNull(
        brElement->GetClosestNativeAnonymousSubtreeRootParentOrHost());
    if (textControlElement &&
        textControlElement->IsSingleLineTextControlOrTextArea()) {
      return a11y::eNoType;
    }
  }

  // If this <br> is a "padding <br> element" used when there is an empty last
  // line before a block boundary in an HTML editor, this is required only for
  // the empty last line visible in the CSS layout world.  Therefore, this is
  // meaningless so that this should not appear in the flattened text.  On the
  // other hand, if this is a padding <br> element used when there is no
  // visible things in the parent block in an editor, this is required to give
  // one-line height to the block.  So, basically, this is meaningless, but
  // this may be surrounded by before/after pseudo content.  Then, they appear
  // in different lines because of this line break.  So, this is not meaningless
  // in such case.  For now, we should treat this is meaningless only in the
  // former case.  We can assume that if this is a padding <br>, it directly
  // follows a block boundary because our editor does not keep empty nodes at
  // least intentionally.
  // XXX This does not treat complicated layout cases.  However, our editor
  // must not work well with such layout.  So, this should be okay for the
  // web apps in the wild.
  nsIFrame* const parentFrame = GetParent();
  if (HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) || !parentFrame) {
    return a11y::eHTMLBRType;
  }
  nsIFrame* const currentBlock =
      nsBlockFrame::GetNearestAncestorBlock(parentFrame);
  nsIContent* const currentBlockContent =
      currentBlock ? currentBlock->GetContent() : nullptr;
  for (nsIContent* previousContent =
           brElement->GetPrevNode(currentBlockContent);
       previousContent;
       previousContent = previousContent->GetPrevNode(currentBlockContent)) {
    nsIFrame* const precedingContentFrame = previousContent->GetPrimaryFrame();
    if (!precedingContentFrame || precedingContentFrame->IsEmpty()) {
      continue;
    }
    if (precedingContentFrame->IsBlockFrameOrSubclass()) {
      break;  // Reached a child block.
    }
    // FIXME: Oh, this should be a11y::eNoType because it's a meaningless <br>.
    return a11y::eHTMLBRType;
  }
  return a11y::eHTMLBRType;
}

#endif
