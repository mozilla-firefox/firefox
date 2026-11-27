/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmencloseFrame.h"

#include <algorithm>

#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/PathHelpers.h"
#include "nsDisplayList.h"
#include "nsLayoutUtils.h"
#include "nsMathMLChar.h"
#include "nsPresContext.h"
#include "nsWhitespaceTokenizer.h"

using namespace mozilla;
using namespace mozilla::gfx;

//
// <menclose> -- enclose content with a stretching symbol such
// as a long division sign. - implementation

// longdiv:
// Unicode 5.1 assigns U+27CC to LONG DIVISION, but a right parenthesis
// renders better with current font support.
static const char16_t kLongDivChar = ')';

// updiagonalstrike
static const uint8_t kArrowHeadSize = 10;

// phasorangle
static const uint8_t kPhasorangleWidth = 8;

nsIFrame* NS_NewMathMLmencloseFrame(PresShell* aPresShell,
                                    ComputedStyle* aStyle) {
  return new nsMathMLmencloseFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmencloseFrame)

nsMathMLmencloseFrame::nsMathMLmencloseFrame(ComputedStyle* aStyle,
                                             nsPresContext* aPresContext)
    : nsMathMLContainerFrame(aStyle, aPresContext, kClassID),
      mRuleThickness(0),
      mLongDivCharIndex(-1),
      mContentWidth(0) {}

nsMathMLmencloseFrame::~nsMathMLmencloseFrame() = default;

nsresult nsMathMLmencloseFrame::AllocateMathMLChar(MencloseNotation mask) {
  // Is the char already allocated?
  if (mask == MencloseNotation::LongDiv && mLongDivCharIndex >= 0) {
    return NS_OK;
  }

  // No need to track the ComputedStyle given to our MathML chars.
  uint32_t i = mMathMLChar.Length();
  nsAutoString Char;

  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier, or change the return type to void.
  mMathMLChar.AppendElement();

  if (mask == MencloseNotation::LongDiv) {
    Char.Assign(kLongDivChar);
    mLongDivCharIndex = i;
  }

  mMathMLChar[i].SetData(Char);
  mMathMLChar[i].SetComputedStyle(Style());

  return NS_OK;
}

/*
 * Add a notation to draw, if the argument is the name of a known notation.
 * @param aNotation string name of a notation
 */
nsresult nsMathMLmencloseFrame::AddNotation(const nsAString& aNotation) {
  nsresult rv;

  if (aNotation.EqualsLiteral("longdiv")) {
    rv = AllocateMathMLChar(MencloseNotation::LongDiv);
    NS_ENSURE_SUCCESS(rv, rv);
    mNotationsToDraw += MencloseNotation::LongDiv;
  } else if (aNotation.EqualsLiteral("actuarial")) {
    mNotationsToDraw += MencloseNotation::Right;
    mNotationsToDraw += MencloseNotation::Top;
  } else if (aNotation.EqualsLiteral("box")) {
    mNotationsToDraw += MencloseNotation::Left;
    mNotationsToDraw += MencloseNotation::Right;
    mNotationsToDraw += MencloseNotation::Top;
    mNotationsToDraw += MencloseNotation::Bottom;
  } else if (aNotation.EqualsLiteral("roundedbox")) {
    mNotationsToDraw += MencloseNotation::RoundedBox;
  } else if (aNotation.EqualsLiteral("circle")) {
    mNotationsToDraw += MencloseNotation::Circle;
  } else if (aNotation.EqualsLiteral("left")) {
    mNotationsToDraw += MencloseNotation::Left;
  } else if (aNotation.EqualsLiteral("right")) {
    mNotationsToDraw += MencloseNotation::Right;
  } else if (aNotation.EqualsLiteral("top")) {
    mNotationsToDraw += MencloseNotation::Top;
  } else if (aNotation.EqualsLiteral("bottom")) {
    mNotationsToDraw += MencloseNotation::Bottom;
  } else if (aNotation.EqualsLiteral("updiagonalstrike")) {
    mNotationsToDraw += MencloseNotation::UpDiagonalStrike;
  } else if (aNotation.EqualsLiteral("updiagonalarrow")) {
    mNotationsToDraw += MencloseNotation::UpDiagonalArrow;
  } else if (aNotation.EqualsLiteral("downdiagonalstrike")) {
    mNotationsToDraw += MencloseNotation::DownDiagonalStrike;
  } else if (aNotation.EqualsLiteral("verticalstrike")) {
    mNotationsToDraw += MencloseNotation::VerticalStrike;
  } else if (aNotation.EqualsLiteral("horizontalstrike")) {
    mNotationsToDraw += MencloseNotation::HorizontalStrike;
  } else if (aNotation.EqualsLiteral("madruwb")) {
    mNotationsToDraw += MencloseNotation::Right;
    mNotationsToDraw += MencloseNotation::Bottom;
  } else if (aNotation.EqualsLiteral("phasorangle")) {
    mNotationsToDraw += MencloseNotation::Bottom;
    mNotationsToDraw += MencloseNotation::PhasorAngle;
  }

  return NS_OK;
}

/*
 * Initialize the list of notations to draw
 */
void nsMathMLmencloseFrame::InitNotations() {
  MarkNeedsDisplayItemRebuild();
  mNotationsToDraw.clear();
  mLongDivCharIndex = -1;
  mMathMLChar.Clear();

  nsAutoString value;

  if (mContent->AsElement()->GetAttr(nsGkAtoms::notation, value)) {
    // parse the notation attribute
    nsWhitespaceTokenizer tokenizer(value);

    while (tokenizer.hasMoreTokens()) {
      AddNotation(tokenizer.nextToken());
    }

    if (IsToDraw(MencloseNotation::UpDiagonalArrow)) {
      // For <menclose notation="updiagonalstrike updiagonalarrow">, if
      // the two notations are drawn then the strike line may cause the point of
      // the arrow to be too wide. Hence we will only draw the updiagonalarrow
      // and the arrow shaft may be thought to be the updiagonalstrike.
      mNotationsToDraw -= MencloseNotation::UpDiagonalStrike;
    }
  } else {
    // default: longdiv
    if (NS_FAILED(AllocateMathMLChar(MencloseNotation::LongDiv))) {
      return;
    }
    mNotationsToDraw += MencloseNotation::LongDiv;
  }
}

NS_IMETHODIMP
nsMathMLmencloseFrame::InheritAutomaticData(nsIFrame* aParent) {
  // let the base class get the default from our parent
  nsMathMLContainerFrame::InheritAutomaticData(aParent);

  mPresentationData.flags +=
      MathMLPresentationFlag::StretchAllChildrenVertically;

  InitNotations();

  return NS_OK;
}

void nsMathMLmencloseFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                             const nsDisplayListSet& aLists) {
  /////////////
  // paint the menclosed content
  nsMathMLContainerFrame::BuildDisplayList(aBuilder, aLists);

  nsRect mencloseRect = nsIFrame::GetContentRectRelativeToSelf();

  if (IsToDraw(MencloseNotation::PhasorAngle)) {
    DisplayNotation(aBuilder, this, mencloseRect, aLists, mRuleThickness,
                    MencloseNotation::PhasorAngle);
  }

  if (IsToDraw(MencloseNotation::LongDiv)) {
    mMathMLChar[mLongDivCharIndex].Display(aBuilder, this, aLists, 1);

    nsRect rect;
    mMathMLChar[mLongDivCharIndex].GetRect(rect);
    rect.SizeTo(rect.width + mContentWidth, mRuleThickness);
    DisplayBar(aBuilder, this, rect, aLists,
               static_cast<uint16_t>(MencloseNotation::LongDiv));
  }

  if (IsToDraw(MencloseNotation::Top)) {
    nsRect rect(0, 0, mencloseRect.width, mRuleThickness);
    DisplayBar(aBuilder, this, rect, aLists,
               static_cast<uint16_t>(MencloseNotation::Top));
  }

  if (IsToDraw(MencloseNotation::Bottom)) {
    nsRect rect(0, mencloseRect.height - mRuleThickness, mencloseRect.width,
                mRuleThickness);
    DisplayBar(aBuilder, this, rect, aLists,
               static_cast<uint16_t>(MencloseNotation::Bottom));
  }

  if (IsToDraw(MencloseNotation::Left)) {
    nsRect rect(0, 0, mRuleThickness, mencloseRect.height);
    DisplayBar(aBuilder, this, rect, aLists,
               static_cast<uint16_t>(MencloseNotation::Left));
  }

  if (IsToDraw(MencloseNotation::Right)) {
    nsRect rect(mencloseRect.width - mRuleThickness, 0, mRuleThickness,
                mencloseRect.height);
    DisplayBar(aBuilder, this, rect, aLists,
               static_cast<uint16_t>(MencloseNotation::Right));
  }

  if (IsToDraw(MencloseNotation::RoundedBox)) {
    DisplayNotation(aBuilder, this, mencloseRect, aLists, mRuleThickness,
                    MencloseNotation::RoundedBox);
  }

  if (IsToDraw(MencloseNotation::Circle)) {
    DisplayNotation(aBuilder, this, mencloseRect, aLists, mRuleThickness,
                    MencloseNotation::Circle);
  }

  if (IsToDraw(MencloseNotation::UpDiagonalStrike)) {
    DisplayNotation(aBuilder, this, mencloseRect, aLists, mRuleThickness,
                    MencloseNotation::UpDiagonalStrike);
  }

  if (IsToDraw(MencloseNotation::UpDiagonalArrow)) {
    DisplayNotation(aBuilder, this, mencloseRect, aLists, mRuleThickness,
                    MencloseNotation::UpDiagonalArrow);
  }

  if (IsToDraw(MencloseNotation::DownDiagonalStrike)) {
    DisplayNotation(aBuilder, this, mencloseRect, aLists, mRuleThickness,
                    MencloseNotation::DownDiagonalStrike);
  }

  if (IsToDraw(MencloseNotation::HorizontalStrike)) {
    nsRect rect(0, mencloseRect.height / 2 - mRuleThickness / 2,
                mencloseRect.width, mRuleThickness);
    DisplayBar(aBuilder, this, rect, aLists,
               static_cast<uint16_t>(MencloseNotation::HorizontalStrike));
  }

  if (IsToDraw(MencloseNotation::VerticalStrike)) {
    nsRect rect(mencloseRect.width / 2 - mRuleThickness / 2, 0, mRuleThickness,
                mencloseRect.height);
    DisplayBar(aBuilder, this, rect, aLists,
               static_cast<uint16_t>(MencloseNotation::VerticalStrike));
  }
}

/* virtual */
void nsMathMLmencloseFrame::Place(DrawTarget* aDrawTarget,
                                  const PlaceFlags& aFlags,
                                  ReflowOutput& aDesiredSize) {
  ///////////////
  // Measure the size of our content using the base class to format like an
  // inferred mrow, without border/padding.
  ReflowOutput baseSize(aDesiredSize.GetWritingMode());
  PlaceFlags flags = aFlags + PlaceFlag::MeasureOnly +
                     PlaceFlag::IgnoreBorderPadding +
                     PlaceFlag::DoNotAdjustForWidthAndHeight;
  nsMathMLContainerFrame::Place(aDrawTarget, flags, baseSize);

  nsBoundingMetrics bmBase = baseSize.mBoundingMetrics;
  nscoord dx_left = 0, dx_right = 0;
  nsBoundingMetrics bmLongdivChar;
  nscoord longdivAscent = 0, longdivDescent = 0;
  nscoord psi = 0;
  nscoord leading = 0;

  ///////////////
  // Thickness of bars and font metrics
  nscoord onePixel = nsPresContext::CSSPixelsToAppUnits(1);

  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(this, fontSizeInflation);
  GetRuleThickness(aDrawTarget, fm, mRuleThickness);
  if (mRuleThickness < onePixel) {
    mRuleThickness = onePixel;
  }

  char16_t one = '1';
  nsBoundingMetrics bmOne =
      nsLayoutUtils::AppUnitBoundsOfString(&one, 1, *fm, aDrawTarget);

  ///////////////
  // General rules: the menclose element takes the size of the enclosed content.
  // We add a padding when needed.

  // determine padding & psi
  nscoord padding = 3 * mRuleThickness;
  nscoord delta = padding % onePixel;
  if (delta) {
    padding += onePixel - delta;  // round up
  }

  if (IsToDraw(MencloseNotation::LongDiv)) {
    // The MathML spec does not define precise layout rules for menclose. Here
    // we draw longdiv using the same parameter as for radicals.
    // See https://github.com/w3c/mathml-core/issues/245
    nscoord dummy;
    GetRadicalParameters(fm, StyleFont()->mMathStyle == StyleMathStyle::Normal,
                         dummy, leading, psi);

    // adjust clearance psi to get an exact number of pixels -- this
    // gives a nicer & uniform look on stacked radicals (bug 130282)
    delta = psi % onePixel;
    if (delta) {
      psi += onePixel - delta;  // round up
    }
  }

  // Set horizontal parameters
  if (IsToDraw(MencloseNotation::RoundedBox) ||
      IsToDraw(MencloseNotation::Top) || IsToDraw(MencloseNotation::Left) ||
      IsToDraw(MencloseNotation::Bottom) ||
      IsToDraw(MencloseNotation::Circle)) {
    dx_left = padding;
  }

  if (IsToDraw(MencloseNotation::RoundedBox) ||
      IsToDraw(MencloseNotation::Top) || IsToDraw(MencloseNotation::Right) ||
      IsToDraw(MencloseNotation::Bottom) ||
      IsToDraw(MencloseNotation::Circle)) {
    dx_right = padding;
  }

  // Set vertical parameters
  if (IsToDraw(MencloseNotation::Right) || IsToDraw(MencloseNotation::Left) ||
      IsToDraw(MencloseNotation::UpDiagonalStrike) ||
      IsToDraw(MencloseNotation::UpDiagonalArrow) ||
      IsToDraw(MencloseNotation::DownDiagonalStrike) ||
      IsToDraw(MencloseNotation::VerticalStrike) ||
      IsToDraw(MencloseNotation::Circle) ||
      IsToDraw(MencloseNotation::RoundedBox) ||
      IsToDraw(MencloseNotation::LongDiv) ||
      IsToDraw(MencloseNotation::PhasorAngle)) {
    // set a minimal value for the base height
    bmBase.ascent = std::max(bmOne.ascent, bmBase.ascent);
    bmBase.descent = std::max(0, bmBase.descent);
  }

  mBoundingMetrics.ascent = bmBase.ascent;
  mBoundingMetrics.descent = bmBase.descent;

  if (IsToDraw(MencloseNotation::RoundedBox) ||
      IsToDraw(MencloseNotation::Top) || IsToDraw(MencloseNotation::Left) ||
      IsToDraw(MencloseNotation::Right) || IsToDraw(MencloseNotation::Circle)) {
    mBoundingMetrics.ascent += padding;
  }

  if (IsToDraw(MencloseNotation::RoundedBox) ||
      IsToDraw(MencloseNotation::Left) || IsToDraw(MencloseNotation::Right) ||
      IsToDraw(MencloseNotation::Bottom) ||
      IsToDraw(MencloseNotation::Circle)) {
    mBoundingMetrics.descent += padding;
  }

  ///////////////
  // phasorangle notation
  if (IsToDraw(MencloseNotation::PhasorAngle)) {
    nscoord phasorangleWidth = kPhasorangleWidth * mRuleThickness;
    // Update horizontal parameters
    dx_left = std::max(dx_left, phasorangleWidth);
  }

  ///////////////
  // updiagonal arrow notation. We need enough space at the top right corner to
  // draw the arrow head.
  if (IsToDraw(MencloseNotation::UpDiagonalArrow)) {
    // This is an estimate, see nsDisplayNotation::Paint for the exact head size
    nscoord arrowHeadSize = kArrowHeadSize * mRuleThickness;

    // We want that the arrow shaft strikes the menclose content and that the
    // arrow head does not overlap with that content. Hence we add some space
    // on the right. We don't add space on the top but only ensure that the
    // ascent is large enough.
    dx_right = std::max(dx_right, arrowHeadSize);
    mBoundingMetrics.ascent = std::max(mBoundingMetrics.ascent, arrowHeadSize);
  }

  ///////////////
  // circle notation: we don't want the ellipse to overlap the enclosed
  // content. Hence, we need to increase the size of the bounding box by a
  // factor of at least sqrt(2).
  if (IsToDraw(MencloseNotation::Circle)) {
    double ratio = (sqrt(2.0) - 1.0) / 2.0;
    nscoord padding2;

    // Update horizontal parameters
    padding2 = ratio * bmBase.width;

    dx_left = std::max(dx_left, padding2);
    dx_right = std::max(dx_right, padding2);

    // Update vertical parameters
    padding2 = ratio * (bmBase.ascent + bmBase.descent);

    mBoundingMetrics.ascent =
        std::max(mBoundingMetrics.ascent, bmBase.ascent + padding2);
    mBoundingMetrics.descent =
        std::max(mBoundingMetrics.descent, bmBase.descent + padding2);
  }

  ///////////////
  // longdiv notation:
  if (IsToDraw(MencloseNotation::LongDiv)) {
    if (aFlags.contains(PlaceFlag::IntrinsicSize)) {
      nscoord longdiv_width = mMathMLChar[mLongDivCharIndex].GetMaxWidth(
          this, aDrawTarget, fontSizeInflation);

      // Update horizontal parameters
      dx_left = std::max(dx_left, longdiv_width);
    } else {
      // Stretch the parenthesis to the appropriate height if it is not
      // big enough.
      nsBoundingMetrics contSize = bmBase;
      contSize.ascent = mRuleThickness;
      contSize.descent = bmBase.ascent + bmBase.descent + psi;

      // height(longdiv) should be >= height(base) + psi + mRuleThickness
      mMathMLChar[mLongDivCharIndex].Stretch(
          this, aDrawTarget, fontSizeInflation, StretchDirection::Vertical,
          contSize, bmLongdivChar, MathMLStretchFlag::Larger, false);
      mMathMLChar[mLongDivCharIndex].GetBoundingMetrics(bmLongdivChar);

      // Update horizontal parameters
      dx_left = std::max(dx_left, bmLongdivChar.width);

      // Update vertical parameters
      longdivAscent = bmBase.ascent + psi + mRuleThickness;
      longdivDescent = std::max(
          bmBase.descent,
          (bmLongdivChar.ascent + bmLongdivChar.descent - longdivAscent));

      mBoundingMetrics.ascent =
          std::max(mBoundingMetrics.ascent, longdivAscent);
      mBoundingMetrics.descent =
          std::max(mBoundingMetrics.descent, longdivDescent);
    }
  }

  ///////////////
  //
  if (IsToDraw(MencloseNotation::Circle) ||
      IsToDraw(MencloseNotation::RoundedBox) ||
      (IsToDraw(MencloseNotation::Left) && IsToDraw(MencloseNotation::Right))) {
    // center the menclose around the content (horizontally)
    dx_left = dx_right = std::max(dx_left, dx_right);
  }

  ///////////////
  // The maximum size is now computed: set the remaining parameters
  mBoundingMetrics.width = dx_left + bmBase.width + dx_right;

  mBoundingMetrics.leftBearing = std::min(0, dx_left + bmBase.leftBearing);
  mBoundingMetrics.rightBearing =
      std::max(mBoundingMetrics.width, dx_left + bmBase.rightBearing);

  aDesiredSize.Width() = mBoundingMetrics.width;

  aDesiredSize.SetBlockStartAscent(
      std::max(mBoundingMetrics.ascent, baseSize.BlockStartAscent()));
  aDesiredSize.Height() =
      aDesiredSize.BlockStartAscent() +
      std::max(mBoundingMetrics.descent,
               baseSize.Height() - baseSize.BlockStartAscent());

  if (IsToDraw(MencloseNotation::LongDiv)) {
    nscoord desiredSizeAscent = aDesiredSize.BlockStartAscent();
    nscoord desiredSizeDescent =
        aDesiredSize.Height() - aDesiredSize.BlockStartAscent();

    if (IsToDraw(MencloseNotation::LongDiv)) {
      desiredSizeAscent = std::max(desiredSizeAscent, longdivAscent + leading);
      desiredSizeDescent =
          std::max(desiredSizeDescent, longdivDescent + mRuleThickness);
    }

    aDesiredSize.SetBlockStartAscent(desiredSizeAscent);
    aDesiredSize.Height() = desiredSizeAscent + desiredSizeDescent;
  }

  if (IsToDraw(MencloseNotation::Circle) ||
      IsToDraw(MencloseNotation::RoundedBox) ||
      (IsToDraw(MencloseNotation::Top) && IsToDraw(MencloseNotation::Bottom))) {
    // center the menclose around the content (vertically)
    nscoord dy = std::max(aDesiredSize.BlockStartAscent() - bmBase.ascent,
                          aDesiredSize.Height() -
                              aDesiredSize.BlockStartAscent() - bmBase.descent);

    aDesiredSize.SetBlockStartAscent(bmBase.ascent + dy);
    aDesiredSize.Height() =
        aDesiredSize.BlockStartAscent() + bmBase.descent + dy;
  }

  // Update mBoundingMetrics ascent/descent
  if (IsToDraw(MencloseNotation::Top) || IsToDraw(MencloseNotation::Right) ||
      IsToDraw(MencloseNotation::Left) ||
      IsToDraw(MencloseNotation::UpDiagonalStrike) ||
      IsToDraw(MencloseNotation::UpDiagonalArrow) ||
      IsToDraw(MencloseNotation::DownDiagonalStrike) ||
      IsToDraw(MencloseNotation::VerticalStrike) ||
      IsToDraw(MencloseNotation::Circle) ||
      IsToDraw(MencloseNotation::RoundedBox)) {
    mBoundingMetrics.ascent = aDesiredSize.BlockStartAscent();
  }

  if (IsToDraw(MencloseNotation::Bottom) || IsToDraw(MencloseNotation::Right) ||
      IsToDraw(MencloseNotation::Left) ||
      IsToDraw(MencloseNotation::UpDiagonalStrike) ||
      IsToDraw(MencloseNotation::UpDiagonalArrow) ||
      IsToDraw(MencloseNotation::DownDiagonalStrike) ||
      IsToDraw(MencloseNotation::VerticalStrike) ||
      IsToDraw(MencloseNotation::Circle) ||
      IsToDraw(MencloseNotation::RoundedBox)) {
    mBoundingMetrics.descent =
        aDesiredSize.Height() - aDesiredSize.BlockStartAscent();
  }

  // phasorangle notation:
  // move up from the bottom by the angled line height
  if (IsToDraw(MencloseNotation::PhasorAngle)) {
    mBoundingMetrics.ascent = std::max(
        mBoundingMetrics.ascent,
        2 * kPhasorangleWidth * mRuleThickness - mBoundingMetrics.descent);
  }

  aDesiredSize.mBoundingMetrics = mBoundingMetrics;

  // Apply width/height to math content box.
  auto sizes = GetWidthAndHeightForPlaceAdjustment(aFlags);
  dx_left += ApplyAdjustmentForWidthAndHeight(aFlags, sizes, aDesiredSize,
                                              mBoundingMetrics);

  // Add padding+border.
  auto borderPadding = GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize,
                                  mBoundingMetrics);

  mReference.x = 0;
  mReference.y = aDesiredSize.BlockStartAscent();

  if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
    //////////////////
    // Set position and size of MathMLChars
    if (IsToDraw(MencloseNotation::LongDiv)) {
      mMathMLChar[mLongDivCharIndex].SetRect(nsRect(
          dx_left - bmLongdivChar.width + borderPadding.left,
          aDesiredSize.BlockStartAscent() - longdivAscent, bmLongdivChar.width,
          bmLongdivChar.ascent + bmLongdivChar.descent));
    }

    mContentWidth = bmBase.width;

    //////////////////
    // Finish reflowing child frames
    PositionRowChildFrames(dx_left + borderPadding.left,
                           aDesiredSize.BlockStartAscent());
  }
}

nscoord nsMathMLmencloseFrame::FixInterFrameSpacing(
    ReflowOutput& aDesiredSize) {
  nscoord gap = nsMathMLContainerFrame::FixInterFrameSpacing(aDesiredSize);
  if (!gap) {
    return 0;
  }

  // Move the MathML characters
  nsRect rect;
  for (uint32_t i = 0; i < mMathMLChar.Length(); i++) {
    mMathMLChar[i].GetRect(rect);
    rect.MoveBy(gap, 0);
    mMathMLChar[i].SetRect(rect);
  }

  return gap;
}

nsresult nsMathMLmencloseFrame::AttributeChanged(int32_t aNameSpaceID,
                                                 nsAtom* aAttribute,
                                                 AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::notation) {
    InitNotations();
    PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                  NS_FRAME_IS_DIRTY);
    return NS_OK;
  }

  return nsMathMLContainerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                                  aModType);
}

void nsMathMLmencloseFrame::DidSetComputedStyle(ComputedStyle* aOldStyle) {
  nsMathMLContainerFrame::DidSetComputedStyle(aOldStyle);
  for (auto& ch : mMathMLChar) {
    ch.SetComputedStyle(Style());
  }
}

//////////////////

namespace mozilla {

class nsDisplayNotation final : public nsPaintedDisplayItem {
 public:
  nsDisplayNotation(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
                    const nsRect& aRect, nscoord aThickness,
                    MencloseNotation aType)
      : nsPaintedDisplayItem(aBuilder, aFrame),
        mRect(aRect),
        mThickness(aThickness),
        mType(aType) {
    MOZ_COUNT_CTOR(nsDisplayNotation);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayNotation)

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override;
  NS_DISPLAY_DECL_NAME("MathMLMencloseNotation", TYPE_MATHML_MENCLOSE_NOTATION)

 private:
  nsRect mRect;
  nscoord mThickness;
  MencloseNotation mType;
};

void nsDisplayNotation::Paint(nsDisplayListBuilder* aBuilder,
                              gfxContext* aCtx) {
  DrawTarget& aDrawTarget = *aCtx->GetDrawTarget();
  nsPresContext* presContext = mFrame->PresContext();

  Float strokeWidth = presContext->AppUnitsToGfxUnits(mThickness);

  Rect rect = NSRectToRect(mRect + ToReferenceFrame(),
                           presContext->AppUnitsPerDevPixel());
  rect.Deflate(strokeWidth / 2.f);

  ColorPattern color(ToDeviceColor(
      mFrame->GetVisitedDependentColor(&nsStyleText::mWebkitTextFillColor)));

  StrokeOptions strokeOptions(strokeWidth);

  switch (mType) {
    case MencloseNotation::Circle: {
      RefPtr<Path> ellipse =
          MakePathForEllipse(aDrawTarget, rect.Center(), rect.Size());
      aDrawTarget.Stroke(ellipse, color, strokeOptions);
      return;
    }
    case MencloseNotation::RoundedBox: {
      Float radius = 3 * strokeWidth;
      RectCornerRadii radii(radius, radius);
      RefPtr<Path> roundedRect =
          MakePathForRoundedRect(aDrawTarget, rect, radii, true);
      aDrawTarget.Stroke(roundedRect, color, strokeOptions);
      return;
    }
    case MencloseNotation::UpDiagonalStrike: {
      aDrawTarget.StrokeLine(rect.BottomLeft(), rect.TopRight(), color,
                             strokeOptions);
      return;
    }
    case MencloseNotation::DownDiagonalStrike: {
      aDrawTarget.StrokeLine(rect.TopLeft(), rect.BottomRight(), color,
                             strokeOptions);
      return;
    }
    case MencloseNotation::UpDiagonalArrow: {
      // Compute some parameters to draw the updiagonalarrow. The values below
      // are taken from MathJax's HTML-CSS output.
      Float W = rect.Width();
      gfxFloat H = rect.Height();
      Float l = sqrt(W * W + H * H);
      Float f = Float(kArrowHeadSize) * strokeWidth / l;
      Float w = W * f;
      gfxFloat h = H * f;

      // Draw the arrow shaft
      aDrawTarget.StrokeLine(rect.BottomLeft(),
                             rect.TopRight() + Point(-.7 * w, .7 * h), color,
                             strokeOptions);

      // Draw the arrow head
      RefPtr<PathBuilder> builder = aDrawTarget.CreatePathBuilder();
      builder->MoveTo(rect.TopRight());
      builder->LineTo(
          rect.TopRight() +
          Point(-w - .4 * h, std::max(-strokeWidth / 2.0, h - .4 * w)));
      builder->LineTo(rect.TopRight() + Point(-.7 * w, .7 * h));
      builder->LineTo(
          rect.TopRight() +
          Point(std::min(strokeWidth / 2.0, -w + .4 * h), h + .4 * w));
      builder->Close();
      RefPtr<Path> path = builder->Finish();
      aDrawTarget.Fill(path, color);
      return;
    }
    case MencloseNotation::PhasorAngle: {
      // Compute some parameters to draw the angled line,
      // that uses a slope of 2 (angle = tan^-1(2)).
      // H = w * tan(angle) = w * 2
      Float w = Float(kPhasorangleWidth) * strokeWidth;
      Float H = 2 * w;

      // Draw the angled line
      aDrawTarget.StrokeLine(rect.BottomLeft(),
                             rect.BottomLeft() + Point(w, -H), color,
                             strokeOptions);
      return;
    }
    default:
      MOZ_ASSERT_UNREACHABLE(
          "This notation can not be drawn using "
          "nsDisplayNotation");
  }
}

}  // namespace mozilla

void nsMathMLmencloseFrame::DisplayNotation(nsDisplayListBuilder* aBuilder,
                                            nsIFrame* aFrame,
                                            const nsRect& aRect,
                                            const nsDisplayListSet& aLists,
                                            nscoord aThickness,
                                            MencloseNotation aType) {
  if (!aFrame->StyleVisibility()->IsVisible() || aRect.IsEmpty() ||
      aThickness <= 0) {
    return;
  }

  aLists.Content()->AppendNewToTopWithIndex<nsDisplayNotation>(
      aBuilder, aFrame, static_cast<uint16_t>(aType), aRect, aThickness, aType);
}
