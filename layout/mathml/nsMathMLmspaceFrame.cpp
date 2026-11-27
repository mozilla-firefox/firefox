/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmspaceFrame.h"

#include <algorithm>

#include "mozilla/PresShell.h"
#include "mozilla/dom/MathMLElement.h"
#include "mozilla/gfx/2D.h"
#include "nsLayoutUtils.h"

using namespace mozilla;

//
// <mspace> -- space - implementation
//

nsIFrame* NS_NewMathMLmspaceFrame(PresShell* aPresShell,
                                  ComputedStyle* aStyle) {
  return new nsMathMLmspaceFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmspaceFrame)

nsMathMLmspaceFrame::~nsMathMLmspaceFrame() = default;

nsresult nsMathMLmspaceFrame::AttributeChanged(int32_t aNameSpaceID,
                                               nsAtom* aAttribute,
                                               AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None) {
    bool hasDirtyAttributes = false;
    IntrinsicDirty intrinsicDirty = IntrinsicDirty::None;
    if (aAttribute == nsGkAtoms::width) {
      mWidth.mState = Attribute::ParsingState::Dirty;
      hasDirtyAttributes = true;
      intrinsicDirty = IntrinsicDirty::FrameAndAncestors;
    } else if (aAttribute == nsGkAtoms::height) {
      mHeight.mState = Attribute::ParsingState::Dirty;
      hasDirtyAttributes = true;
    } else if (aAttribute == nsGkAtoms::depth) {
      mDepth.mState = Attribute::ParsingState::Dirty;
      hasDirtyAttributes = true;
    }
    if (hasDirtyAttributes) {
      PresShell()->FrameNeedsReflow(this, intrinsicDirty, NS_FRAME_IS_DIRTY);
    }
    return NS_OK;
  }
  return nsMathMLContainerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                                  aModType);
}

nscoord nsMathMLmspaceFrame::CalculateAttributeValue(
    nsAtom* aAtom, Attribute& aAttribute, float aFontSizeInflation,
    dom::MathMLElement::ParseFlags aFlags) {
  if (aAttribute.mState == Attribute::ParsingState::Dirty) {
    nsAutoString value;
    aAttribute.mState = Attribute::ParsingState::Invalid;
    mContent->AsElement()->GetAttr(aAtom, value);
    if (!value.IsEmpty()) {
      if (dom::MathMLElement::ParseNumericValue(
              value, aAttribute.mValue, PresContext()->Document(), aFlags)) {
        aAttribute.mState = Attribute::ParsingState::Valid;
      } else {
        ReportParseError(aAtom->GetUTF16String(), value.get());
      }
    }
  }
  // Invalid is interpreted as the default which is 0.
  // Percentages are interpreted as a multiple of the default value.
  if (aAttribute.mState == Attribute::ParsingState::Invalid ||
      aAttribute.mValue.GetUnit() == eCSSUnit_Percent) {
    return 0;
  }
  return CalcLength(aAttribute.mValue, aFontSizeInflation, this);
}

void nsMathMLmspaceFrame::Place(DrawTarget* aDrawTarget,
                                const PlaceFlags& aFlags,
                                ReflowOutput& aDesiredSize) {
  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);

  // <mspace/> is listed among MathML elements allowing negative spacing and
  // the MathML test suite contains "Presentation/TokenElements/mspace/mspace2"
  // as an example. Hence we allow negative values.
  nscoord width =
      CalculateAttributeValue(nsGkAtoms::width, mWidth, fontSizeInflation,
                              dom::MathMLElement::ParseFlag::AllowNegative);

  // We do not allow negative values for height and depth attributes. See bug
  // 716349.
  nscoord height =
      CalculateAttributeValue(nsGkAtoms::height, mHeight, fontSizeInflation);
  nscoord depth =
      CalculateAttributeValue(nsGkAtoms::depth, mDepth, fontSizeInflation);

  mBoundingMetrics = nsBoundingMetrics();
  mBoundingMetrics.width = width;
  mBoundingMetrics.ascent = height;
  mBoundingMetrics.descent = depth;
  mBoundingMetrics.leftBearing = 0;
  mBoundingMetrics.rightBearing = mBoundingMetrics.width;

  aDesiredSize.SetBlockStartAscent(mBoundingMetrics.ascent);
  aDesiredSize.Width() = std::max(0, mBoundingMetrics.width);
  aDesiredSize.Height() = mBoundingMetrics.ascent + mBoundingMetrics.descent;
  // Also return our bounding metrics
  aDesiredSize.mBoundingMetrics = mBoundingMetrics;

  // Apply width/height to math content box.
  const PlaceFlags flags;
  auto sizes = GetWidthAndHeightForPlaceAdjustment(flags);
  ApplyAdjustmentForWidthAndHeight(flags, sizes, aDesiredSize,
                                   mBoundingMetrics);

  // Add padding+border.
  auto borderPadding = GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize,
                                  mBoundingMetrics);
}
