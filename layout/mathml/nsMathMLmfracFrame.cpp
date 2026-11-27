/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmfracFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "gfxMathTable.h"
#include "gfxTextRun.h"
#include "gfxUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MathMLElement.h"
#include "mozilla/gfx/2D.h"
#include "nsDisplayList.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"

using namespace mozilla;
using namespace mozilla::gfx;

//
// <mfrac> -- form a fraction from two subexpressions - implementation
//

nsIFrame* NS_NewMathMLmfracFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsMathMLmfracFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmfracFrame)

nsMathMLmfracFrame::~nsMathMLmfracFrame() = default;

MathMLFrameType nsMathMLmfracFrame::GetMathMLFrameType() {
  // frac is "inner" in TeXBook, Appendix G, rule 15e. See also page 170.
  return MathMLFrameType::Inner;
}

uint8_t nsMathMLmfracFrame::ScriptIncrement(nsIFrame* aFrame) {
  if (StyleFont()->mMathStyle == StyleMathStyle::Compact && aFrame &&
      (mFrames.FirstChild() == aFrame || mFrames.LastChild() == aFrame)) {
    return 1;
  }
  return 0;
}

NS_IMETHODIMP
nsMathMLmfracFrame::TransmitAutomaticData() {
  // If displaystyle is false, then scriptlevel is incremented, so notify the
  // children of this.
  if (StyleFont()->mMathStyle == StyleMathStyle::Compact) {
    PropagateFrameFlagFor(mFrames.FirstChild(),
                          NS_FRAME_MATHML_SCRIPT_DESCENDANT);
    PropagateFrameFlagFor(mFrames.LastChild(),
                          NS_FRAME_MATHML_SCRIPT_DESCENDANT);
  }

  // if our numerator is an embellished operator, let its state bubble to us
  GetEmbellishDataFrom(mFrames.FirstChild(), mEmbellishData);
  if (mEmbellishData.flags.contains(MathMLEmbellishFlag::EmbellishedOperator)) {
    // even when embellished, we need to record that <mfrac> won't fire
    // Stretch() on its embellished child
    mEmbellishData.direction = StretchDirection::Unsupported;
  }

  return NS_OK;
}

nscoord nsMathMLmfracFrame::CalcLineThickness(nsString& aThicknessAttribute,
                                              nscoord onePixel,
                                              nscoord aDefaultRuleThickness,
                                              float aFontSizeInflation) {
  nscoord defaultThickness = aDefaultRuleThickness;
  nscoord lineThickness = aDefaultRuleThickness;
  nscoord minimumThickness = onePixel;

  // linethickness
  // https://w3c.github.io/mathml-core/#dfn-linethickness
  if (!aThicknessAttribute.IsEmpty()) {
    lineThickness = defaultThickness;
    ParseAndCalcNumericValue(aThicknessAttribute, &lineThickness,
                             aFontSizeInflation, this,
                             dom::MathMLElement::ParseFlag::AllowNegative);
    // MathML Core says a negative value is interpreted as 0.
    if (lineThickness < 0) {
      lineThickness = 0;
    }
  }
  // use minimum if the lineThickness is a non-zero value less than minimun
  if (lineThickness && lineThickness < minimumThickness) {
    lineThickness = minimumThickness;
  }

  return lineThickness;
}

void nsMathMLmfracFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                          const nsDisplayListSet& aLists) {
  /////////////
  // paint the numerator and denominator
  nsMathMLContainerFrame::BuildDisplayList(aBuilder, aLists);

  /////////////
  // paint the fraction line
  DisplayBar(aBuilder, this, mLineRect, aLists);
}

nsresult nsMathMLmfracFrame::AttributeChanged(int32_t aNameSpaceID,
                                              nsAtom* aAttribute,
                                              AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      nsGkAtoms::linethickness == aAttribute) {
    // The thickness changes, so a repaint of the bar is needed.
    InvalidateFrame();
    // The thickness affects vertical offsets.
    PresShell()->FrameNeedsReflow(this, IntrinsicDirty::None,
                                  NS_FRAME_IS_DIRTY);
    return NS_OK;
  }
  return nsMathMLContainerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                                  aModType);
}

nscoord nsMathMLmfracFrame::FixInterFrameSpacing(ReflowOutput& aDesiredSize) {
  nscoord gap = nsMathMLContainerFrame::FixInterFrameSpacing(aDesiredSize);
  if (!gap) {
    return 0;
  }

  mLineRect.MoveBy(gap, 0);
  return gap;
}

/* virtual */
void nsMathMLmfracFrame::Place(DrawTarget* aDrawTarget,
                               const PlaceFlags& aFlags,
                               ReflowOutput& aDesiredSize) {
  ////////////////////////////////////
  // Get the children's desired sizes
  nsBoundingMetrics bmNum, bmDen;
  ReflowOutput sizeNum(aDesiredSize.GetWritingMode());
  ReflowOutput sizeDen(aDesiredSize.GetWritingMode());
  nsIFrame* frameDen = nullptr;
  nsIFrame* frameNum = mFrames.FirstChild();
  if (frameNum) {
    frameDen = frameNum->GetNextSibling();
  }
  if (!frameNum || !frameDen || frameDen->GetNextSibling()) {
    // report an error, encourage people to get their markups in order
    if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
      ReportChildCountError();
    }
    return PlaceAsMrow(aDrawTarget, aFlags, aDesiredSize);
  }
  GetReflowAndBoundingMetricsFor(frameNum, sizeNum, bmNum);
  GetReflowAndBoundingMetricsFor(frameDen, sizeDen, bmDen);

  nsMargin numMargin = GetMarginForPlace(aFlags, frameNum),
           denMargin = GetMarginForPlace(aFlags, frameDen);

  nsPresContext* presContext = PresContext();
  nscoord onePixel = nsPresContext::CSSPixelsToAppUnits(1);

  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(this, fontSizeInflation);

  nscoord defaultRuleThickness, axisHeight;
  nscoord oneDevPixel = fm->AppUnitsPerDevPixel();
  RefPtr<gfxFont> mathFont = fm->GetThebesFontGroup()->GetFirstMathFont();
  if (mathFont) {
    defaultRuleThickness = mathFont->MathTable()->Constant(
        gfxMathTable::FractionRuleThickness, oneDevPixel);
  } else {
    GetRuleThickness(aDrawTarget, fm, defaultRuleThickness);
  }
  GetAxisHeight(aDrawTarget, fm, axisHeight);

  bool outermostEmbellished = false;
  if (mEmbellishData.coreFrame) {
    nsEmbellishData parentData;
    GetEmbellishDataFrom(GetParent(), parentData);
    outermostEmbellished = parentData.coreFrame != mEmbellishData.coreFrame;
  }

  // see if the linethickness attribute is there
  nsAutoString value;
  mContent->AsElement()->GetAttr(nsGkAtoms::linethickness, value);
  mLineThickness = CalcLineThickness(value, onePixel, defaultRuleThickness,
                                     fontSizeInflation);

  bool displayStyle = StyleFont()->mMathStyle == StyleMathStyle::Normal;

  mLineRect.height = mLineThickness;

  // Add lspace & rspace that may come from <mo> if we are an outermost
  // embellished container (we fetch values from the core since they may use
  // units that depend on style data, and style changes could have occurred
  // in the core since our last visit there)
  nscoord leftSpace = 0;
  nscoord rightSpace = 0;
  if (!StaticPrefs::
          mathml_lspace_rspace_for_child_spacing_during_mrow_layout_enabled() &&
      outermostEmbellished) {
    const bool isRTL = GetWritingMode().IsBidiRTL();
    nsEmbellishData coreData;
    GetEmbellishDataFrom(mEmbellishData.coreFrame, coreData);
    leftSpace += isRTL ? coreData.trailingSpace : coreData.leadingSpace;
    rightSpace += isRTL ? coreData.leadingSpace : coreData.trailingSpace;
  }

  nscoord actualRuleThickness = mLineThickness;

  //////////////////
  // Get shifts
  nscoord numShift = 0;
  nscoord denShift = 0;

  // Rule 15b, App. G, TeXbook
  nscoord numShift1, numShift2, numShift3;
  nscoord denShift1, denShift2;

  GetNumeratorShifts(fm, numShift1, numShift2, numShift3);
  GetDenominatorShifts(fm, denShift1, denShift2);

  if (0 == actualRuleThickness) {
    numShift = displayStyle ? numShift1 : numShift3;
    denShift = displayStyle ? denShift1 : denShift2;
    if (mathFont) {
      numShift = mathFont->MathTable()->Constant(
          displayStyle ? gfxMathTable::StackTopDisplayStyleShiftUp
                       : gfxMathTable::StackTopShiftUp,
          oneDevPixel);
      denShift = mathFont->MathTable()->Constant(
          displayStyle ? gfxMathTable::StackBottomDisplayStyleShiftDown
                       : gfxMathTable::StackBottomShiftDown,
          oneDevPixel);
    }
  } else {
    numShift = displayStyle ? numShift1 : numShift2;
    denShift = displayStyle ? denShift1 : denShift2;
    if (mathFont) {
      numShift = mathFont->MathTable()->Constant(
          displayStyle ? gfxMathTable::FractionNumeratorDisplayStyleShiftUp
                       : gfxMathTable::FractionNumeratorShiftUp,
          oneDevPixel);
      denShift = mathFont->MathTable()->Constant(
          displayStyle ? gfxMathTable::FractionDenominatorDisplayStyleShiftDown
                       : gfxMathTable::FractionDenominatorShiftDown,
          oneDevPixel);
    }
  }

  if (0 == actualRuleThickness) {
    // Rule 15c, App. G, TeXbook

    // min clearance between numerator and denominator
    nscoord minClearance =
        displayStyle ? 7 * defaultRuleThickness : 3 * defaultRuleThickness;
    if (mathFont) {
      minClearance = mathFont->MathTable()->Constant(
          displayStyle ? gfxMathTable::StackDisplayStyleGapMin
                       : gfxMathTable::StackGapMin,
          oneDevPixel);
    }

    nscoord actualClearance = (numShift - bmNum.descent - numMargin.bottom) -
                              (bmDen.ascent + denMargin.top - denShift);
    // actualClearance should be >= minClearance
    if (actualClearance < minClearance) {
      nscoord halfGap = (minClearance - actualClearance) / 2;
      numShift += halfGap;
      denShift += halfGap;
    }
  } else {
    // Rule 15d, App. G, TeXbook

    // min clearance between numerator or denominator and middle of bar

    // TeX has a different interpretation of the thickness.
    // Try $a \above10pt b$ to see. Here is what TeX does:
    // minClearance = displayStyle ?
    //   3 * actualRuleThickness : actualRuleThickness;

    // we slightly depart from TeX here. We use the defaultRuleThickness
    // instead of the value coming from the linethickness attribute, i.e., we
    // recover what TeX does if the user hasn't set linethickness. But when
    // the linethickness is set, we avoid the wide gap problem.
    nscoord minClearanceNum = displayStyle ? 3 * defaultRuleThickness
                                           : defaultRuleThickness + onePixel;
    nscoord minClearanceDen = minClearanceNum;
    if (mathFont) {
      minClearanceNum = mathFont->MathTable()->Constant(
          displayStyle ? gfxMathTable::FractionNumDisplayStyleGapMin
                       : gfxMathTable::FractionNumeratorGapMin,
          oneDevPixel);
      minClearanceDen = mathFont->MathTable()->Constant(
          displayStyle ? gfxMathTable::FractionDenomDisplayStyleGapMin
                       : gfxMathTable::FractionDenominatorGapMin,
          oneDevPixel);
    }

    // adjust numShift to maintain minClearanceNum if needed
    nscoord actualClearanceNum = (numShift - bmNum.descent - numMargin.bottom) -
                                 (axisHeight + actualRuleThickness / 2);
    if (actualClearanceNum < minClearanceNum) {
      numShift += (minClearanceNum - actualClearanceNum);
    }
    // adjust denShift to maintain minClearanceDen if needed
    nscoord actualClearanceDen = (axisHeight - actualRuleThickness / 2) -
                                 (bmDen.ascent + denMargin.top - denShift);
    if (actualClearanceDen < minClearanceDen) {
      denShift += (minClearanceDen - actualClearanceDen);
    }
  }

  //////////////////
  // Place Children

  // XXX Need revisiting the width. TeX uses the exact width
  // e.g. in $$\huge\frac{\displaystyle\int}{i}$$
  nscoord width = std::max(bmNum.width + numMargin.LeftRight(),
                           bmDen.width + denMargin.LeftRight());
  nscoord dxNum =
      leftSpace + (width - sizeNum.Width() - numMargin.LeftRight()) / 2;
  nscoord dxDen =
      leftSpace + (width - sizeDen.Width() - denMargin.LeftRight()) / 2;
  width += leftSpace + rightSpace;

  mBoundingMetrics.rightBearing =
      std::max(dxNum + bmNum.rightBearing + numMargin.LeftRight(),
               dxDen + bmDen.rightBearing + denMargin.LeftRight());
  if (mBoundingMetrics.rightBearing < width - rightSpace) {
    mBoundingMetrics.rightBearing = width - rightSpace;
  }
  mBoundingMetrics.leftBearing =
      std::min(dxNum + bmNum.leftBearing, dxDen + bmDen.leftBearing);
  if (mBoundingMetrics.leftBearing > leftSpace) {
    mBoundingMetrics.leftBearing = leftSpace;
  }
  mBoundingMetrics.ascent = bmNum.ascent + numShift + numMargin.top;
  mBoundingMetrics.descent = bmDen.descent + denShift + denMargin.bottom;
  mBoundingMetrics.width = width;

  aDesiredSize.SetBlockStartAscent(numMargin.top + sizeNum.BlockStartAscent() +
                                   numShift);
  aDesiredSize.Height() = aDesiredSize.BlockStartAscent() + sizeDen.Height() +
                          denMargin.bottom - sizeDen.BlockStartAscent() +
                          denShift;
  aDesiredSize.Width() = mBoundingMetrics.width;
  aDesiredSize.mBoundingMetrics = mBoundingMetrics;

  // Apply width/height to math content box.
  auto sizes = GetWidthAndHeightForPlaceAdjustment(aFlags);
  auto shiftX = ApplyAdjustmentForWidthAndHeight(aFlags, sizes, aDesiredSize,
                                                 mBoundingMetrics);
  if (sizes.width) {
    // MathML Core says the math content box is horizontally centered
    // but the fraction bar still takes the full width of the content box.
    dxNum += shiftX;
    dxDen += shiftX;
    width = *sizes.width;
  }

  // Add padding+border.
  auto borderPadding = GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize,
                                  mBoundingMetrics);
  leftSpace += borderPadding.left;
  rightSpace += borderPadding.right;
  width += borderPadding.LeftRight();
  dxNum += borderPadding.left;
  dxDen += borderPadding.left;

  mReference.x = 0;
  mReference.y = aDesiredSize.BlockStartAscent();

  if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
    nscoord dy;
    // place numerator
    dxNum += numMargin.left;
    dy = borderPadding.top + numMargin.top;
    FinishReflowChild(frameNum, presContext, sizeNum, nullptr, dxNum, dy,
                      ReflowChildFlags::Default);
    // place denominator
    dxDen += denMargin.left;
    dy =
        aDesiredSize.BlockStartAscent() + denShift - sizeDen.BlockStartAscent();
    FinishReflowChild(frameDen, presContext, sizeDen, nullptr, dxDen, dy,
                      ReflowChildFlags::Default);
    // place the fraction bar - dy is top of bar
    dy = aDesiredSize.BlockStartAscent() -
         (axisHeight + actualRuleThickness / 2);
    mLineRect.SetRect(leftSpace, dy, width - (leftSpace + rightSpace),
                      actualRuleThickness);
  }
}
