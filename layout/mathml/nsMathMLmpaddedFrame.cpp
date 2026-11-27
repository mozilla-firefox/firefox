/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmpaddedFrame.h"

#include <algorithm>

#include "mozilla/PresShell.h"
#include "mozilla/TextUtils.h"
#include "mozilla/dom/MathMLElement.h"
#include "mozilla/gfx/2D.h"
#include "nsLayoutUtils.h"

using namespace mozilla;

//
// <mpadded> -- adjust space around content - implementation
//

nsIFrame* NS_NewMathMLmpaddedFrame(PresShell* aPresShell,
                                   ComputedStyle* aStyle) {
  return new nsMathMLmpaddedFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmpaddedFrame)

nsMathMLmpaddedFrame::~nsMathMLmpaddedFrame() = default;

NS_IMETHODIMP
nsMathMLmpaddedFrame::InheritAutomaticData(nsIFrame* aParent) {
  // let the base class get the default from our parent
  nsMathMLContainerFrame::InheritAutomaticData(aParent);

  mPresentationData.flags +=
      MathMLPresentationFlag::StretchAllChildrenVertically;

  return NS_OK;
}

nsresult nsMathMLmpaddedFrame::AttributeChanged(int32_t aNameSpaceID,
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
    } else if (aAttribute == nsGkAtoms::lspace) {
      mLeadingSpace.mState = Attribute::ParsingState::Dirty;
      hasDirtyAttributes = true;
      intrinsicDirty = IntrinsicDirty::FrameAndAncestors;
    } else if (aAttribute == nsGkAtoms::voffset) {
      mVerticalOffset.mState = Attribute::ParsingState::Dirty;
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

void nsMathMLmpaddedFrame::ParseAttribute(nsAtom* aAtom,
                                          Attribute& aAttribute) {
  if (aAttribute.mState != Attribute::ParsingState::Dirty) {
    return;
  }
  nsAutoString value;
  aAttribute.mState = Attribute::ParsingState::Invalid;
  mContent->AsElement()->GetAttr(aAtom, value);
  if (!value.IsEmpty()) {
    if (!ParseAttribute(value, aAttribute)) {
      ReportParseError(aAtom->GetUTF16String(), value.get());
    }
  }
}

bool nsMathMLmpaddedFrame::ParseAttribute(nsString& aString,
                                          Attribute& aAttribute) {
  // See https://www.w3.org/TR/MathML3/chapter3.html#presm.mpaddedatt
  aAttribute.Reset();
  aAttribute.mState = Attribute::ParsingState::Invalid;

  aString.CompressWhitespace();  // aString is not a const in this code

  int32_t stringLength = aString.Length();
  if (!stringLength) {
    return false;
  }

  nsAutoString number, unit;

  //////////////////////
  // see if the sign is there

  int32_t i = 0;

  if (aString[0] == '+') {
    aAttribute.mSign = Attribute::Sign::Plus;
    i++;
  } else if (aString[0] == '-') {
    aAttribute.mSign = Attribute::Sign::Minus;
    i++;
  } else {
    aAttribute.mSign = Attribute::Sign::Unspecified;
  }

  // get the number
  bool gotDot = false, gotPercent = false;
  for (; i < stringLength; i++) {
    char16_t c = aString[i];
    if (gotDot && c == '.') {
      // error - two dots encountered
      return false;
    }

    if (c == '.') {
      gotDot = true;
    } else if (!IsAsciiDigit(c)) {
      break;
    }
    number.Append(c);
  }

  // catch error if we didn't enter the loop above... we could simply initialize
  // floatValue = 1, to cater for cases such as width="height", but that
  // wouldn't be in line with the spec which requires an explicit number
  if (number.IsEmpty()) {
    return false;
  }

  nsresult errorCode;
  float floatValue = number.ToFloat(&errorCode);
  if (NS_FAILED(errorCode)) {
    return false;
  }

  // see if this is a percentage-based value
  if (i < stringLength && aString[i] == '%') {
    i++;
    gotPercent = true;
  }

  // the remainder now should be a css-unit, or a pseudo-unit, or a named-space
  aString.Right(unit, stringLength - i);

  if (unit.IsEmpty()) {
    if (gotPercent) {
      // case ["+"|"-"] unsigned-number "%"
      aAttribute.mValue.SetPercentValue(floatValue / 100.0f);
      aAttribute.mPseudoUnit = Attribute::PseudoUnit::ItSelf;
      aAttribute.mState = Attribute::ParsingState::Valid;
      return true;
    } else {
      // case ["+"|"-"] unsigned-number
      // XXXfredw: should we allow non-zero unitless values? See bug 757703.
      if (!floatValue) {
        aAttribute.mValue.SetFloatValue(floatValue, eCSSUnit_Number);
        aAttribute.mPseudoUnit = Attribute::PseudoUnit::ItSelf;
        aAttribute.mState = Attribute::ParsingState::Valid;
        return true;
      }
    }
  } else if (unit.EqualsLiteral("width")) {
    aAttribute.mPseudoUnit = Attribute::PseudoUnit::Width;
  } else if (unit.EqualsLiteral("height")) {
    aAttribute.mPseudoUnit = Attribute::PseudoUnit::Height;
  } else if (unit.EqualsLiteral("depth")) {
    aAttribute.mPseudoUnit = Attribute::PseudoUnit::Depth;
  } else if (!gotPercent) {  // percentage can only apply to a pseudo-unit

    // see if the unit is a named-space
    if (dom::MathMLElement::ParseNamedSpaceValue(
            unit, aAttribute.mValue, *mContent->OwnerDoc(),
            dom::MathMLElement::ParseFlag::AllowNegative)) {
      // re-scale properly, and we know that the unit of the named-space is 'em'
      floatValue *= aAttribute.mValue.GetFloatValue();
      aAttribute.mValue.SetFloatValue(floatValue, eCSSUnit_EM);
      aAttribute.mPseudoUnit = Attribute::PseudoUnit::NamedSpace;
      aAttribute.mState = Attribute::ParsingState::Valid;
      return true;
    }

    // see if the input was just a CSS value
    // We are not supposed to have a unitless, percent, negative or namedspace
    // value here.
    number.Append(unit);  // leave the sign out if it was there
    if (dom::MathMLElement::ParseNumericValue(
            number, aAttribute.mValue, nullptr,
            dom::MathMLElement::ParseFlag::SuppressWarnings)) {
      aAttribute.mState = Attribute::ParsingState::Valid;
      return true;
    }
  }

  // if we enter here, we have a number that will act as a multiplier on a
  // pseudo-unit
  if (aAttribute.mPseudoUnit != Attribute::PseudoUnit::Unspecified) {
    if (gotPercent) {
      aAttribute.mValue.SetPercentValue(floatValue / 100.0f);
    } else {
      aAttribute.mValue.SetFloatValue(floatValue, eCSSUnit_Number);
    }

    aAttribute.mState = Attribute::ParsingState::Valid;
    return true;
  }

#ifdef DEBUG
  printf("mpadded: attribute with bad numeric value: %s\n",
         NS_LossyConvertUTF16toASCII(aString).get());
#endif
  // if we reach here, it means we encounter an unexpected input
  return false;
}

void nsMathMLmpaddedFrame::UpdateValue(const Attribute& aAttribute,
                                       Attribute::PseudoUnit aSelfUnit,
                                       const ReflowOutput& aDesiredSize,
                                       nscoord& aValueToUpdate,
                                       float aFontSizeInflation) {
  nsCSSUnit unit = aAttribute.mValue.GetUnit();
  if (aAttribute.IsValid() && eCSSUnit_Null != unit) {
    nscoord scaler = 0, amount = 0;

    if (eCSSUnit_Percent == unit || eCSSUnit_Number == unit) {
      auto pseudoUnit = aAttribute.mPseudoUnit;
      if (pseudoUnit == Attribute::PseudoUnit::ItSelf) {
        pseudoUnit = aSelfUnit;
      }
      switch (pseudoUnit) {
        case Attribute::PseudoUnit::Width:
          scaler = aDesiredSize.Width();
          break;

        case Attribute::PseudoUnit::Height:
          scaler = aDesiredSize.BlockStartAscent();
          break;

        case Attribute::PseudoUnit::Depth:
          scaler = aDesiredSize.Height() - aDesiredSize.BlockStartAscent();
          break;

        default:
          // if we ever reach here, it would mean something is wrong
          // somewhere with the setup and/or the caller
          NS_ERROR("Unexpected Pseudo Unit");
          return;
      }
    }

    if (eCSSUnit_Number == unit) {
      amount =
          NSToCoordRound(float(scaler) * aAttribute.mValue.GetFloatValue());
    } else if (eCSSUnit_Percent == unit) {
      amount =
          NSToCoordRound(float(scaler) * aAttribute.mValue.GetPercentValue());
    } else {
      amount = CalcLength(aAttribute.mValue, aFontSizeInflation, this);
    }

    switch (aAttribute.mSign) {
      case Attribute::Sign::Plus:
        aValueToUpdate += amount;
        break;
      case Attribute::Sign::Minus:
        aValueToUpdate -= amount;
        break;
      case Attribute::Sign::Unspecified:
        aValueToUpdate = amount;
        break;
    }
  }
}

/* virtual */
void nsMathMLmpaddedFrame::Place(DrawTarget* aDrawTarget,
                                 const PlaceFlags& aFlags,
                                 ReflowOutput& aDesiredSize) {
  // First perform normal row layout without border/padding.
  PlaceFlags flags = aFlags + PlaceFlag::MeasureOnly +
                     PlaceFlag::IgnoreBorderPadding +
                     PlaceFlag::DoNotAdjustForWidthAndHeight;
  nsMathMLContainerFrame::Place(aDrawTarget, flags, aDesiredSize);

  nscoord height = aDesiredSize.BlockStartAscent();
  nscoord depth = aDesiredSize.Height() - aDesiredSize.BlockStartAscent();
  // The REC says:
  //
  // "The lspace attribute ('leading' space) specifies the horizontal location
  // of the positioning point of the child content with respect to the
  // positioning point of the mpadded element. By default they coincide, and
  // therefore absolute values for lspace have the same effect as relative
  // values."
  //
  // "MathML renderers should ensure that, except for the effects of the
  // attributes, the relative spacing between the contents of the mpadded
  // element and surrounding MathML elements would not be modified by replacing
  // an mpadded element with an mrow element with the same content, even if
  // linebreaking occurs within the mpadded element."
  //
  // (http://www.w3.org/TR/MathML/chapter3.html#presm.mpadded)
  //
  // "In those discussions, the terms leading and trailing are used to specify
  // a side of an object when which side to use depends on the directionality;
  // ie. leading means left in LTR but right in RTL."
  // (http://www.w3.org/TR/MathML/chapter3.html#presm.bidi.math)
  nscoord lspace = 0;
  // In MathML3, "width" will be the bounding box width and "advancewidth" will
  // refer "to the horizontal distance between the positioning point of the
  // mpadded and the positioning point for the following content".  MathML2
  // doesn't make the distinction.
  nscoord width = aDesiredSize.Width();
  nscoord voffset = 0;

  nscoord initialWidth = width;
  float fontSizeInflation = nsLayoutUtils::FontSizeInflationFor(this);

  // update width
  ParseAttribute(nsGkAtoms::width, mWidth);
  UpdateValue(mWidth, Attribute::PseudoUnit::Width, aDesiredSize, width,
              fontSizeInflation);
  width = std::max(0, width);

  // update "height" (this is the ascent in the terminology of the REC)
  ParseAttribute(nsGkAtoms::height, mHeight);
  UpdateValue(mHeight, Attribute::PseudoUnit::Height, aDesiredSize, height,
              fontSizeInflation);
  height = std::max(0, height);

  // update "depth" (this is the descent in the terminology of the REC)
  ParseAttribute(nsGkAtoms::depth, mDepth);
  UpdateValue(mDepth, Attribute::PseudoUnit::Depth, aDesiredSize, depth,
              fontSizeInflation);
  depth = std::max(0, depth);

  // update lspace
  ParseAttribute(nsGkAtoms::lspace, mLeadingSpace);
  if (mLeadingSpace.mPseudoUnit != Attribute::PseudoUnit::ItSelf) {
    UpdateValue(mLeadingSpace, Attribute::PseudoUnit::Unspecified, aDesiredSize,
                lspace, fontSizeInflation);
  }

  // update voffset
  ParseAttribute(nsGkAtoms::voffset, mVerticalOffset);
  if (mVerticalOffset.mPseudoUnit != Attribute::PseudoUnit::ItSelf) {
    UpdateValue(mVerticalOffset, Attribute::PseudoUnit::Unspecified,
                aDesiredSize, voffset, fontSizeInflation);
  }

  // do the padding now that we have everything
  // The idea here is to maintain the invariant that <mpadded>...</mpadded>
  // (i.e., with no attributes) looks the same as <mrow>...</mrow>. But when
  // there are attributes, tweak our metrics and move children to achieve the
  // desired visual effects.

  const bool isRTL = GetWritingMode().IsBidiRTL();
  if (isRTL ? mWidth.IsValid() : mLeadingSpace.IsValid()) {
    // there was padding on the left. dismiss the left italic correction now
    // (so that our parent won't correct us)
    mBoundingMetrics.leftBearing = 0;
  }

  if (isRTL ? mLeadingSpace.IsValid() : mWidth.IsValid()) {
    // there was padding on the right. dismiss the right italic correction now
    // (so that our parent won't correct us)
    mBoundingMetrics.width = width;
    mBoundingMetrics.rightBearing = mBoundingMetrics.width;
  }

  nscoord dx = (isRTL ? width - initialWidth - lspace : lspace);

  aDesiredSize.SetBlockStartAscent(height);
  aDesiredSize.Width() = mBoundingMetrics.width;
  aDesiredSize.Height() = depth + aDesiredSize.BlockStartAscent();
  mBoundingMetrics.ascent = height;
  mBoundingMetrics.descent = depth;
  aDesiredSize.mBoundingMetrics = mBoundingMetrics;

  // Apply width/height to math content box.
  auto sizes = GetWidthAndHeightForPlaceAdjustment(aFlags);
  dx += ApplyAdjustmentForWidthAndHeight(aFlags, sizes, aDesiredSize,
                                         mBoundingMetrics);

  // Add padding+border.
  auto borderPadding = GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize,
                                  mBoundingMetrics);
  dx += borderPadding.left;

  mReference.x = 0;
  mReference.y = aDesiredSize.BlockStartAscent();

  if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
    // Finish reflowing child frames, positioning their origins.
    PositionRowChildFrames(dx, aDesiredSize.BlockStartAscent() - voffset);
  }
}
