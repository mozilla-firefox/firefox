/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmrootFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "gfxMathTable.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"

using namespace mozilla;

//
// <mroot> -- form a radical - implementation
//

static const char16_t kSqrChar = char16_t(0x221A);

nsIFrame* NS_NewMathMLmrootFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsMathMLmrootFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmrootFrame)

nsMathMLmrootFrame::nsMathMLmrootFrame(ComputedStyle* aStyle,
                                       nsPresContext* aPresContext)
    : nsMathMLContainerFrame(aStyle, aPresContext, kClassID) {}

nsMathMLmrootFrame::~nsMathMLmrootFrame() = default;

void nsMathMLmrootFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                              nsIFrame* aPrevInFlow) {
  nsMathMLContainerFrame::Init(aContent, aParent, aPrevInFlow);

  nsAutoString sqrChar;
  sqrChar.Assign(kSqrChar);
  mSqrChar.SetData(sqrChar);
  mSqrChar.SetComputedStyle(Style());
}

bool nsMathMLmrootFrame::ShouldUseRowFallback() {
  bool isRootWithIndex = GetContent()->IsMathMLElement(nsGkAtoms::mroot);
  if (!isRootWithIndex) {
    return false;
  }
  // An mroot element expects exactly two children.
  nsIFrame* baseFrame = mFrames.FirstChild();
  if (!baseFrame) {
    return true;
  }
  nsIFrame* indexFrame = baseFrame->GetNextSibling();
  return !indexFrame || indexFrame->GetNextSibling();
}

bool nsMathMLmrootFrame::IsMrowLike() {
  bool isRootWithIndex = GetContent()->IsMathMLElement(nsGkAtoms::mroot);
  if (isRootWithIndex) {
    return false;
  }
  return mFrames.FirstChild() != mFrames.LastChild() || !mFrames.FirstChild();
}

NS_IMETHODIMP
nsMathMLmrootFrame::InheritAutomaticData(nsIFrame* aParent) {
  nsMathMLContainerFrame::InheritAutomaticData(aParent);

  bool isRootWithIndex = GetContent()->IsMathMLElement(nsGkAtoms::mroot);
  if (!isRootWithIndex) {
    mPresentationData.flags +=
        MathMLPresentationFlag::StretchAllChildrenVertically;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMathMLmrootFrame::TransmitAutomaticData() {
  bool isRootWithIndex = GetContent()->IsMathMLElement(nsGkAtoms::mroot);
  if (isRootWithIndex) {
    PropagateFrameFlagFor(mFrames.LastChild(),
                          NS_FRAME_MATHML_SCRIPT_DESCENDANT);
  }

  return NS_OK;
}

void nsMathMLmrootFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                          const nsDisplayListSet& aLists) {
  /////////////
  // paint the content we are square-rooting
  nsMathMLContainerFrame::BuildDisplayList(aBuilder, aLists);

  if (ShouldUseRowFallback()) {
    return;
  }

  /////////////
  // paint the sqrt symbol
  mSqrChar.Display(aBuilder, this, aLists, 0);

  DisplayBar(aBuilder, this, mBarRect, aLists);
}

void nsMathMLmrootFrame::GetRadicalXOffsets(nscoord aIndexWidth,
                                            nscoord aSqrWidth,
                                            nsFontMetrics* aFontMetrics,
                                            nscoord* aIndexOffset,
                                            nscoord* aSqrOffset) {
  // The index is tucked in closer to the radical while making sure
  // that the kern does not make the index and radical collide
  nscoord dxIndex, dxSqr, radicalKernBeforeDegree, radicalKernAfterDegree;
  nscoord oneDevPixel = aFontMetrics->AppUnitsPerDevPixel();
  RefPtr<gfxFont> mathFont =
      aFontMetrics->GetThebesFontGroup()->GetFirstMathFont();

  if (mathFont) {
    radicalKernBeforeDegree = mathFont->MathTable()->Constant(
        gfxMathTable::RadicalKernBeforeDegree, oneDevPixel);
    radicalKernAfterDegree = mathFont->MathTable()->Constant(
        gfxMathTable::RadicalKernAfterDegree, oneDevPixel);
  } else {
    nscoord em;
    GetEmHeight(aFontMetrics, em);
    radicalKernBeforeDegree = NSToCoordRound(5.0f * em / 18);
    radicalKernAfterDegree = NSToCoordRound(-10.0f * em / 18);
  }

  // Clamp radical kern degrees according to spec:
  // https://w3c.github.io/mathml-core/#root-with-index
  radicalKernBeforeDegree = std::max(0, radicalKernBeforeDegree);
  radicalKernAfterDegree = std::max(-aIndexWidth, radicalKernAfterDegree);

  dxIndex = radicalKernBeforeDegree;
  dxSqr = radicalKernBeforeDegree + aIndexWidth + radicalKernAfterDegree;
  if (aIndexOffset) {
    *aIndexOffset = dxIndex;
  }
  if (aSqrOffset) {
    *aSqrOffset = dxSqr;
  }
}

void nsMathMLmrootFrame::Place(DrawTarget* aDrawTarget,
                               const PlaceFlags& aFlags,
                               ReflowOutput& aDesiredSize) {
  if (ShouldUseRowFallback()) {
    // report an error, encourage people to get their markups in order
    if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
      ReportChildCountError();
    }
    return PlaceAsMrow(aDrawTarget, aFlags, aDesiredSize);
  }

  const bool isRootWithIndex = GetContent()->IsMathMLElement(nsGkAtoms::mroot);
  nsBoundingMetrics bmSqr, bmBase, bmIndex;
  nsIFrame *baseFrame = nullptr, *indexFrame = nullptr;
  nsMargin baseMargin, indexMargin;
  ReflowOutput baseSize(aDesiredSize.GetWritingMode());
  ReflowOutput indexSize(aDesiredSize.GetWritingMode());
  if (isRootWithIndex) {
    baseFrame = mFrames.FirstChild();
    indexFrame = baseFrame->GetNextSibling();
    baseMargin = GetMarginForPlace(aFlags, baseFrame);
    indexMargin = GetMarginForPlace(aFlags, indexFrame);
    GetReflowAndBoundingMetricsFor(baseFrame, baseSize, bmBase);
    GetReflowAndBoundingMetricsFor(indexFrame, indexSize, bmIndex);
  } else {
    // Format our content as an mrow without border/padding to obtain the
    // square root base. The metrics/frame for the index are ignored.
    PlaceFlags flags = aFlags + PlaceFlag::MeasureOnly +
                       PlaceFlag::IgnoreBorderPadding +
                       PlaceFlag::DoNotAdjustForWidthAndHeight;
    nsMathMLContainerFrame::Place(aDrawTarget, flags, baseSize);
    bmBase = baseSize.mBoundingMetrics;
  }

  ////////////
  // Prepare the radical symbol and the overline bar

  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(this, fontSizeInflation);

  nscoord ruleThickness, leading, psi;
  GetRadicalParameters(fm, StyleFont()->mMathStyle == StyleMathStyle::Normal,
                       ruleThickness, leading, psi);

  // built-in: adjust clearance psi to emulate \mathstrut using '1' (TexBook,
  // p.131)
  char16_t one = '1';
  nsBoundingMetrics bmOne =
      nsLayoutUtils::AppUnitBoundsOfString(&one, 1, *fm, aDrawTarget);
  if (bmOne.ascent > bmBase.ascent + baseMargin.top) {
    psi += bmOne.ascent - bmBase.ascent - baseMargin.top;
  }

  // make sure that the rule appears on on screen
  nscoord onePixel = nsPresContext::CSSPixelsToAppUnits(1);
  if (ruleThickness < onePixel) {
    ruleThickness = onePixel;
  }

  // adjust clearance psi to get an exact number of pixels -- this
  // gives a nicer & uniform look on stacked radicals (bug 130282)
  nscoord delta = psi % onePixel;
  if (delta) {
    psi += onePixel - delta;  // round up
  }

  // Stretch the radical symbol to the appropriate height if it is not big
  // enough.
  nsBoundingMetrics contSize = bmBase;
  contSize.descent =
      bmBase.ascent + bmBase.descent + baseMargin.TopBottom() + psi;
  contSize.ascent = ruleThickness;

  // height(radical) should be >= height(base) + psi + ruleThickness
  nsBoundingMetrics radicalSize;
  if (aFlags.contains(PlaceFlag::IntrinsicSize)) {
    nscoord radical_width =
        mSqrChar.GetMaxWidth(this, aDrawTarget, fontSizeInflation);
    bmSqr.leftBearing = 0;
    bmSqr.rightBearing = radical_width;
    bmSqr.width = radical_width;
    bmSqr.ascent = bmSqr.descent = 0;
  } else {
    mSqrChar.Stretch(this, aDrawTarget, fontSizeInflation,
                     StretchDirection::Vertical, contSize, radicalSize,
                     MathMLStretchFlag::Larger, GetWritingMode().IsBidiRTL());
    // radicalSize have changed at this point, and should match with
    // the bounding metrics of the char
    mSqrChar.GetBoundingMetrics(bmSqr);
  }

  // Update the desired size for the container (like msqrt, index is not yet
  // included) the baseline will be that of the base.
  mBoundingMetrics.ascent =
      bmBase.ascent + baseMargin.top + psi + ruleThickness;
  mBoundingMetrics.descent =
      std::max(bmBase.descent + baseMargin.bottom,
               (bmSqr.ascent + bmSqr.descent - mBoundingMetrics.ascent));
  mBoundingMetrics.width = bmSqr.width + bmBase.width + baseMargin.LeftRight();
  mBoundingMetrics.leftBearing = bmSqr.leftBearing;
  mBoundingMetrics.rightBearing =
      bmSqr.width +
      std::max(
          bmBase.width + baseMargin.LeftRight(),
          bmBase.rightBearing + baseMargin.left);  // take also care of the rule

  aDesiredSize.SetBlockStartAscent(mBoundingMetrics.ascent + leading);
  aDesiredSize.Height() =
      aDesiredSize.BlockStartAscent() +
      std::max(baseSize.Height() - baseSize.BlockStartAscent(),
               mBoundingMetrics.descent + ruleThickness);
  aDesiredSize.Width() = mBoundingMetrics.width;

  nscoord indexClearance = 0, dxIndex = 0, dxSqr = 0, indexRaisedAscent = 0;
  if (isRootWithIndex) {
    /////////////
    // Re-adjust the desired size to include the index.

    // the index is raised by some fraction of the height
    // of the radical, see \mroot macro in App. B, TexBook
    float raiseIndexPercent = 0.6f;
    RefPtr<gfxFont> mathFont = fm->GetThebesFontGroup()->GetFirstMathFont();
    if (mathFont) {
      raiseIndexPercent = mathFont->MathTable()->Constant(
          gfxMathTable::RadicalDegreeBottomRaisePercent);
    }
    nscoord raiseIndexDelta =
        NSToCoordRound(raiseIndexPercent * (bmSqr.ascent + bmSqr.descent));
    indexRaisedAscent = mBoundingMetrics.ascent  // top of radical
                        -
                        (bmSqr.ascent + bmSqr.descent)  // to bottom of radical
                        + raiseIndexDelta + bmIndex.ascent + bmIndex.descent +
                        indexMargin.TopBottom();  // to top of raised index

    if (mBoundingMetrics.ascent < indexRaisedAscent) {
      indexClearance =
          indexRaisedAscent -
          mBoundingMetrics.ascent;  // excess gap introduced by a tall index
      mBoundingMetrics.ascent = indexRaisedAscent;
      nscoord descent = aDesiredSize.Height() - aDesiredSize.BlockStartAscent();
      aDesiredSize.SetBlockStartAscent(mBoundingMetrics.ascent + leading);
      aDesiredSize.Height() = aDesiredSize.BlockStartAscent() + descent;
    }

    GetRadicalXOffsets(bmIndex.width + indexMargin.LeftRight(), bmSqr.width, fm,
                       &dxIndex, &dxSqr);

    mBoundingMetrics.width =
        dxSqr + bmSqr.width + bmBase.width + baseMargin.LeftRight();
    mBoundingMetrics.leftBearing =
        std::min(dxIndex + bmIndex.leftBearing, dxSqr + bmSqr.leftBearing);
    mBoundingMetrics.rightBearing =
        dxSqr + bmSqr.width +
        std::max(bmBase.width + baseMargin.LeftRight(),
                 bmBase.rightBearing + baseMargin.left);

    aDesiredSize.Width() = mBoundingMetrics.width;
  }

  aDesiredSize.mBoundingMetrics = mBoundingMetrics;

  // Apply width/height to math content box.
  const PlaceFlags flags;
  auto sizes = GetWidthAndHeightForPlaceAdjustment(flags);
  nscoord shiftX = ApplyAdjustmentForWidthAndHeight(flags, sizes, aDesiredSize,
                                                    mBoundingMetrics);

  // Add padding+border around the final layout.
  auto borderPadding = GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize,
                                  mBoundingMetrics);

  if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
    nsPresContext* presContext = PresContext();
    const bool isRTL = GetWritingMode().IsBidiRTL();
    nscoord borderPaddingInlineStart =
        isRTL ? borderPadding.right : borderPadding.left;
    nscoord dx, dy;

    if (isRootWithIndex) {
      // place the index
      dx = borderPaddingInlineStart + dxIndex +
           indexMargin.Side(isRTL ? eSideRight : eSideLeft);
      dy = aDesiredSize.BlockStartAscent() -
           (indexRaisedAscent + indexSize.BlockStartAscent() - bmIndex.ascent);
      FinishReflowChild(
          indexFrame, presContext, indexSize, nullptr,
          MirrorIfRTL(aDesiredSize.Width(), indexSize.Width(), dx),
          dy + indexMargin.top, ReflowChildFlags::Default);
    }

    // place the radical symbol and the radical bar
    dx = borderPaddingInlineStart + dxSqr;
    dy = borderPadding.top + indexClearance +
         leading;  // leave a leading at the top
    mSqrChar.SetRect(nsRect(MirrorIfRTL(aDesiredSize.Width(), bmSqr.width, dx),
                            dy, bmSqr.width, bmSqr.ascent + bmSqr.descent));
    dx += bmSqr.width;
    mBarRect.SetRect(MirrorIfRTL(aDesiredSize.Width(),
                                 bmBase.width + baseMargin.LeftRight(), dx),
                     dy, bmBase.width + baseMargin.LeftRight(), ruleThickness);

    // place the base
    if (isRootWithIndex) {
      dx += isRTL ? baseMargin.right : baseMargin.left;
      dy = aDesiredSize.BlockStartAscent() - baseSize.BlockStartAscent();
      FinishReflowChild(baseFrame, presContext, baseSize, nullptr,
                        MirrorIfRTL(aDesiredSize.Width(), baseSize.Width(), dx),
                        dy, ReflowChildFlags::Default);
    } else {
      nscoord dx_left = borderPadding.left + shiftX;
      if (!isRTL) {
        dx_left += bmSqr.width;
      }
      PositionRowChildFrames(dx_left, aDesiredSize.BlockStartAscent());
    }
  }

  mReference.x = 0;
  mReference.y = aDesiredSize.BlockStartAscent();
}

void nsMathMLmrootFrame::DidSetComputedStyle(ComputedStyle* aOldStyle) {
  nsMathMLContainerFrame::DidSetComputedStyle(aOldStyle);
  mSqrChar.SetComputedStyle(Style());
}
