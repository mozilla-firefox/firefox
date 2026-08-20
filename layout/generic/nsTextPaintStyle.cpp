/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTextPaintStyle.h"

#include "mozilla/LookAndFeel.h"
#include "mozilla/RelativeLuminanceUtils.h"
#include "nsCSSColorUtils.h"
#include "nsCSSRendering.h"
#include "nsFrameSelection.h"
#include "nsLayoutUtils.h"
#include "nsStyleConsts.h"
#include "nsTextFrame.h"

using namespace mozilla;
using namespace mozilla::dom;

static nscolor EnsureDifferentColors(nscolor colorA, nscolor colorB) {
  if (colorA == colorB) {
    nscolor res;
    res = NS_RGB(NS_GET_R(colorA) ^ 0xff, NS_GET_G(colorA) ^ 0xff,
                 NS_GET_B(colorA) ^ 0xff);
    return res;
  }
  return colorA;
}

nsTextPaintStyle::nsTextPaintStyle(nsTextFrame* aFrame)
    : mFrame(aFrame),
      mPresContext(aFrame->PresContext()),
      mInitCommonColors(false),
      mInitSelectionColorsAndShadow(false),
      mResolveColors(true),
      mInitTargetTextPseudoStyle(false),
      mSelectionTextColor(NS_RGBA(0, 0, 0, 0)),
      mSelectionBGColor(NS_RGBA(0, 0, 0, 0)),
      mSufficientContrast(0),
      mFrameBackgroundColor(NS_RGBA(0, 0, 0, 0)),
      mSystemFieldForegroundColor(NS_RGBA(0, 0, 0, 0)),
      mSystemFieldBackgroundColor(NS_RGBA(0, 0, 0, 0)) {}

bool nsTextPaintStyle::EnsureSufficientContrast(nscolor* aForeColor,
                                                nscolor* aBackColor) {
  InitCommonColors();

  const bool sameAsForeground = *aForeColor == NS_SAME_AS_FOREGROUND_COLOR;
  if (sameAsForeground) {
    *aForeColor = GetTextColor();
  }

  // If the combination of selection background color and frame background color
  // has sufficient contrast, don't exchange the selection colors.
  //
  // Note we use a different threshold here: mSufficientContrast is for contrast
  // between text and background colors, but since we're diffing two
  // backgrounds, we don't need that much contrast.  We match the heuristic from
  // NS_SUFFICIENT_LUMINOSITY_DIFFERENCE_BG and use 20% of mSufficientContrast.
  const int32_t minLuminosityDifferenceForBackground = mSufficientContrast / 5;
  const int32_t backLuminosityDifference =
      NS_LUMINOSITY_DIFFERENCE(*aBackColor, mFrameBackgroundColor);
  if (backLuminosityDifference >= minLuminosityDifferenceForBackground) {
    return false;
  }

  // Otherwise, we should use the higher-contrast color for the selection
  // background color.
  //
  // For NS_SAME_AS_FOREGROUND_COLOR we only do this if the background is
  // totally indistinguishable, that is, if the luminosity difference is 0.
  if (sameAsForeground && backLuminosityDifference) {
    return false;
  }

  int32_t foreLuminosityDifference =
      NS_LUMINOSITY_DIFFERENCE(*aForeColor, mFrameBackgroundColor);
  if (backLuminosityDifference < foreLuminosityDifference) {
    std::swap(*aForeColor, *aBackColor);
    // Ensure foreground color is opaque to guarantee contrast.
    *aForeColor = NS_RGB(NS_GET_R(*aForeColor), NS_GET_G(*aForeColor),
                         NS_GET_B(*aForeColor));
    return true;
  }
  return false;
}

nscolor nsTextPaintStyle::GetTextColor() {
  if (mFrame->IsInSVGTextSubtree()) {
    if (!mResolveColors) {
      return NS_SAME_AS_FOREGROUND_COLOR;
    }

    const nsStyleSVG* style = mFrame->StyleSVG();
    switch (style->mFill.kind.tag) {
      case StyleSVGPaintKind::Tag::None:
        return NS_RGBA(0, 0, 0, 0);
      case StyleSVGPaintKind::Tag::Color:
        return nsLayoutUtils::GetTextColor(mFrame, &nsStyleSVG::mFill);
      default:
        NS_ERROR("cannot resolve SVG paint to nscolor");
        return NS_RGBA(0, 0, 0, 255);
    }
  }

  return nsLayoutUtils::GetTextColor(mFrame,
                                     &nsStyleText::mWebkitTextFillColor);
}

bool nsTextPaintStyle::GetSelectionColors(nscolor* aForeColor,
                                          nscolor* aBackColor) {
  NS_ASSERTION(aForeColor, "aForeColor is null");
  NS_ASSERTION(aBackColor, "aBackColor is null");

  if (!InitSelectionColorsAndShadow()) {
    return false;
  }

  *aForeColor = mSelectionTextColor;
  *aBackColor = mSelectionBGColor;
  return true;
}

void nsTextPaintStyle::GetHighlightColors(nscolor* aForeColor,
                                          nscolor* aBackColor) {
  NS_ASSERTION(aForeColor, "aForeColor is null");
  NS_ASSERTION(aBackColor, "aBackColor is null");

  const nsFrameSelection* frameSelection = mFrame->GetConstFrameSelection();
  const Selection* selection =
      frameSelection->GetSelection(SelectionType::eFind);
  const SelectionCustomColors* customColors = nullptr;
  if (selection) {
    customColors = selection->GetCustomColors();
  }

  if (!customColors) {
    nscolor backColor = LookAndFeel::Color(
        LookAndFeel::ColorID::TextHighlightBackground, mFrame);
    nscolor foreColor = LookAndFeel::Color(
        LookAndFeel::ColorID::TextHighlightForeground, mFrame);
    EnsureSufficientContrast(&foreColor, &backColor);
    *aForeColor = foreColor;
    *aBackColor = backColor;
    return;
  }

  if (customColors->mForegroundColor && customColors->mBackgroundColor) {
    nscolor foreColor = *customColors->mForegroundColor;
    nscolor backColor = *customColors->mBackgroundColor;

    if (EnsureSufficientContrast(&foreColor, &backColor) &&
        customColors->mAltForegroundColor &&
        customColors->mAltBackgroundColor) {
      foreColor = *customColors->mAltForegroundColor;
      backColor = *customColors->mAltBackgroundColor;
    }

    *aForeColor = foreColor;
    *aBackColor = backColor;
    return;
  }

  InitCommonColors();

  if (customColors->mBackgroundColor) {
    // !mForegroundColor means "currentColor"; the current color of the text.
    nscolor foreColor = GetTextColor();
    nscolor backColor = *customColors->mBackgroundColor;

    int32_t luminosityDifference =
        NS_LUMINOSITY_DIFFERENCE(foreColor, backColor);

    if (mSufficientContrast > luminosityDifference &&
        customColors->mAltBackgroundColor) {
      int32_t altLuminosityDifference = NS_LUMINOSITY_DIFFERENCE(
          foreColor, *customColors->mAltBackgroundColor);

      if (luminosityDifference < altLuminosityDifference) {
        backColor = *customColors->mAltBackgroundColor;
      }
    }

    *aForeColor = foreColor;
    *aBackColor = backColor;
    return;
  }

  if (customColors->mForegroundColor) {
    nscolor foreColor = *customColors->mForegroundColor;
    // !mBackgroundColor means "transparent"; the current color of the
    // background.

    int32_t luminosityDifference =
        NS_LUMINOSITY_DIFFERENCE(foreColor, mFrameBackgroundColor);

    if (mSufficientContrast > luminosityDifference &&
        customColors->mAltForegroundColor) {
      int32_t altLuminosityDifference = NS_LUMINOSITY_DIFFERENCE(
          *customColors->mForegroundColor, mFrameBackgroundColor);

      if (luminosityDifference < altLuminosityDifference) {
        foreColor = *customColors->mAltForegroundColor;
      }
    }

    *aForeColor = foreColor;
    *aBackColor = NS_TRANSPARENT;
    return;
  }

  // There are neither mForegroundColor nor mBackgroundColor.
  *aForeColor = GetTextColor();
  *aBackColor = NS_TRANSPARENT;
}

bool nsTextPaintStyle::TargetTextUseLightScheme() {
  if (mTargetTextUseLightScheme.isSome()) {
    return *mTargetTextUseLightScheme;
  }
  InitCommonColors();
  const auto darkSchemeBackground =
      LookAndFeel::Color(LookAndFeel::ColorID::TargetTextBackground,
                         ColorScheme::Dark, LookAndFeel::UseStandins::No);
  const auto lightSchemeBackground =
      LookAndFeel::Color(LookAndFeel::ColorID::TargetTextBackground,
                         ColorScheme::Light, LookAndFeel::UseStandins::No);
  mTargetTextUseLightScheme =
      Some(RelativeLuminanceUtils::ContrastRatio(lightSchemeBackground,
                                                 mFrameBackgroundColor) >
           RelativeLuminanceUtils::ContrastRatio(darkSchemeBackground,
                                                 mFrameBackgroundColor));
  return *mTargetTextUseLightScheme;
}

bool nsTextPaintStyle::GetTargetTextColor(nscolor* aForeColor) {
  NS_ASSERTION(aForeColor, "aForeColor is null");
  InitTargetTextPseudoStyle();
  if (mTargetTextPseudoStyle &&
      (mTargetTextPseudoStyle->HasAuthorSpecifiedTextColor() ||
       mTargetTextPseudoStyle->HasAuthorSpecifiedBorderOrBackground())) {
    *aForeColor = mTargetTextPseudoStyle->GetVisitedDependentColor(
        &nsStyleText::mWebkitTextFillColor);
    return mTargetTextPseudoStyle->HasAuthorSpecifiedTextColor();
  }
  *aForeColor = LookAndFeel::Color(
      LookAndFeel::ColorID::TargetTextForeground,
      TargetTextUseLightScheme() ? ColorScheme::Light : ColorScheme::Dark,
      LookAndFeel::UseStandins::No);
  return false;
}

bool nsTextPaintStyle::GetTargetTextBackgroundColor(nscolor* aBackColor) {
  NS_ASSERTION(aBackColor, "aBackColor is null");
  InitTargetTextPseudoStyle();
  if (mTargetTextPseudoStyle &&
      (mTargetTextPseudoStyle->HasAuthorSpecifiedTextColor() ||
       mTargetTextPseudoStyle->HasAuthorSpecifiedBorderOrBackground())) {
    *aBackColor = mTargetTextPseudoStyle->GetVisitedDependentColor(
        &nsStyleBackground::mBackgroundColor);
    return NS_GET_A(*aBackColor) != 0;
  }
  *aBackColor = LookAndFeel::Color(
      LookAndFeel::ColorID::TargetTextBackground,
      TargetTextUseLightScheme() ? ColorScheme::Light : ColorScheme::Dark,
      LookAndFeel::UseStandins::No);
  return NS_GET_A(*aBackColor) != 0;
}

mozilla::Span<const StyleSimpleShadow> nsTextPaintStyle::GetTargetTextShadow() {
  InitTargetTextPseudoStyle();

  if (mTargetTextPseudoStyle &&
      mTargetTextPseudoStyle->HasAuthorSpecifiedTextShadow()) {
    return mTargetTextPseudoStyle->StyleText()->mTextShadow.AsSpan();
  }

  return {};
}

bool nsTextPaintStyle::GetCustomHighlightTextColor(nsAtom* aHighlightName,
                                                   nscolor* aForeColor) {
  NS_ASSERTION(aForeColor, "aForeColor is null");

  // non-existing highlights will be stored as `aHighlightName->nullptr`,
  // so subsequent calls only need a hashtable lookup and don't have
  // to enter the style engine.
  RefPtr<ComputedStyle> highlightStyle =
      mCustomHighlightPseudoStyles.LookupOrInsertWith(
          aHighlightName, [this, &aHighlightName] {
            return mFrame->ComputeHighlightSelectionStyle(aHighlightName);
          });
  if (!highlightStyle) {
    // highlight `aHighlightName` doesn't exist or has no style rules.
    return false;
  }

  *aForeColor = highlightStyle->GetVisitedDependentColor(&nsStyleText::mColor);

  return highlightStyle->HasAuthorSpecifiedTextColor();
}

bool nsTextPaintStyle::GetCustomHighlightBackgroundColor(nsAtom* aHighlightName,
                                                         nscolor* aBackColor) {
  NS_ASSERTION(aBackColor, "aBackColor is null");
  // non-existing highlights will be stored as `aHighlightName->nullptr`,
  // so subsequent calls only need a hashtable lookup and don't have
  // to enter the style engine.
  RefPtr<ComputedStyle> highlightStyle =
      mCustomHighlightPseudoStyles.LookupOrInsertWith(
          aHighlightName, [this, &aHighlightName] {
            return mFrame->ComputeHighlightSelectionStyle(aHighlightName);
          });
  if (!highlightStyle) {
    // highlight `aHighlightName` doesn't exist or has no style rules.
    return false;
  }

  *aBackColor = highlightStyle->GetVisitedDependentColor(
      &nsStyleBackground::mBackgroundColor);
  return NS_GET_A(*aBackColor) != 0;
}

mozilla::Span<const StyleSimpleShadow>
nsTextPaintStyle::GetCustomHighlightTextShadow(nsAtom* aHighlightName) {
  RefPtr<ComputedStyle> highlightStyle =
      mCustomHighlightPseudoStyles.LookupOrInsertWith(
          aHighlightName, [this, &aHighlightName] {
            return mFrame->ComputeHighlightSelectionStyle(aHighlightName);
          });
  if (!highlightStyle || !highlightStyle->HasAuthorSpecifiedTextShadow()) {
    return {};
  }

  return highlightStyle->StyleText()->mTextShadow.AsSpan();
}

RefPtr<ComputedStyle> nsTextPaintStyle::GetComputedStyleForSelectionPseudo(
    SelectionType aSelectionType, nsAtom* aHighlightName) {
  switch (aSelectionType) {
    case SelectionType::eNormal:
      InitSelectionColorsAndShadow();
      return mSelectionPseudoStyle;
    case SelectionType::eTargetText:
      InitTargetTextPseudoStyle();
      return mTargetTextPseudoStyle;
    case SelectionType::eHighlight: {
      return mCustomHighlightPseudoStyles.LookupOrInsertWith(
          aHighlightName, [this, &aHighlightName] {
            return mFrame->ComputeHighlightSelectionStyle(aHighlightName);
          });
    }
    default:
      return nullptr;
  }
}

void nsTextPaintStyle::GetURLSecondaryColor(nscolor* aForeColor) {
  NS_ASSERTION(aForeColor, "aForeColor is null");

  const nscolor textColor = GetTextColor();
  *aForeColor = NS_RGBA(NS_GET_R(textColor), NS_GET_G(textColor),
                        NS_GET_B(textColor), 178);
}

void nsTextPaintStyle::GetIMESelectionColors(SelectionStyleIndex aIndex,
                                             nscolor* aForeColor,
                                             nscolor* aBackColor) {
  NS_ASSERTION(aForeColor, "aForeColor is null");
  NS_ASSERTION(aBackColor, "aBackColor is null");

  nsSelectionStyle* selectionStyle = SelectionStyle(aIndex);
  *aForeColor = selectionStyle->mTextColor;
  *aBackColor = selectionStyle->mBGColor;
}

bool nsTextPaintStyle::GetSelectionUnderlineForPaint(
    SelectionStyleIndex aIndex, nscolor* aLineColor, float* aRelativeSize,
    StyleTextDecorationStyle* aStyle) {
  NS_ASSERTION(aLineColor, "aLineColor is null");
  NS_ASSERTION(aRelativeSize, "aRelativeSize is null");

  nsSelectionStyle* selectionStyle = SelectionStyle(aIndex);
  if (selectionStyle->mUnderlineStyle == StyleTextDecorationStyle::None ||
      selectionStyle->mUnderlineColor == NS_TRANSPARENT ||
      selectionStyle->mUnderlineRelativeSize <= 0.0f) {
    return false;
  }

  *aLineColor = selectionStyle->mUnderlineColor;
  *aRelativeSize = selectionStyle->mUnderlineRelativeSize;
  *aStyle = selectionStyle->mUnderlineStyle;
  return true;
}

void nsTextPaintStyle::InitCommonColors() {
  if (mInitCommonColors) {
    return;
  }

  auto bgColor = nsCSSRendering::FindEffectiveBackgroundColor(mFrame);
  mFrameBackgroundColor = bgColor.mColor;

  mSystemFieldForegroundColor =
      LookAndFeel::Color(LookAndFeel::ColorID::Fieldtext, mFrame);
  mSystemFieldBackgroundColor =
      LookAndFeel::Color(LookAndFeel::ColorID::Field, mFrame);

  if (bgColor.mIsThemed) {
    // Assume a native widget has sufficient contrast always
    mSufficientContrast = 0;
    mInitCommonColors = true;
    return;
  }

  nscolor defaultWindowBackgroundColor =
      LookAndFeel::Color(LookAndFeel::ColorID::Window, mFrame);
  nscolor selectionTextColor =
      LookAndFeel::Color(LookAndFeel::ColorID::Highlighttext, mFrame);
  nscolor selectionBGColor =
      LookAndFeel::Color(LookAndFeel::ColorID::Highlight, mFrame);

  mSufficientContrast = std::min(
      std::min(NS_SUFFICIENT_LUMINOSITY_DIFFERENCE,
               NS_LUMINOSITY_DIFFERENCE(selectionTextColor, selectionBGColor)),
      NS_LUMINOSITY_DIFFERENCE(defaultWindowBackgroundColor, selectionBGColor));

  mInitCommonColors = true;
}

nscolor nsTextPaintStyle::GetSystemFieldForegroundColor() {
  InitCommonColors();
  return mSystemFieldForegroundColor;
}

nscolor nsTextPaintStyle::GetSystemFieldBackgroundColor() {
  InitCommonColors();
  return mSystemFieldBackgroundColor;
}

bool nsTextPaintStyle::InitSelectionColorsAndShadow() {
  if (mInitSelectionColorsAndShadow) {
    return true;
  }

  int16_t selectionFlags;
  const int16_t selectionStatus = mFrame->GetSelectionStatus(&selectionFlags);
  if (!(selectionFlags & nsISelectionDisplay::DISPLAY_TEXT) ||
      selectionStatus < nsISelectionController::SELECTION_ON) {
    // Not displaying the normal selection.
    // We're not caching this fact, so every call to GetSelectionColors
    // will come through here. We could avoid this, but it's not really worth
    // it.
    return false;
  }

  mInitSelectionColorsAndShadow = true;

  // Use ::selection pseudo class if applicable.
  if (RefPtr<ComputedStyle> style =
          mFrame->ComputeSelectionStyle(selectionStatus)) {
    mSelectionPseudoStyle = std::move(style);
    if (mSelectionPseudoStyle->HasAuthorSpecifiedTextColor() ||
        mSelectionPseudoStyle->HasAuthorSpecifiedBorderOrBackground()) {
      mSelectionBGColor = mSelectionPseudoStyle->GetVisitedDependentColor(
          &nsStyleBackground::mBackgroundColor);
      mSelectionTextColor =
          mSelectionPseudoStyle->GetVisitedDependentColor(&nsStyleText::mColor);
      return true;
    }
  }

  mSelectionTextColor =
      LookAndFeel::Color(LookAndFeel::ColorID::Highlighttext, mFrame);

  nscolor selectionBGColor =
      LookAndFeel::Color(LookAndFeel::ColorID::Highlight, mFrame);

  switch (selectionStatus) {
    case nsISelectionController::SELECTION_ATTENTION: {
      mSelectionTextColor = LookAndFeel::Color(
          LookAndFeel::ColorID::TextSelectAttentionForeground, mFrame);
      mSelectionBGColor = LookAndFeel::Color(
          LookAndFeel::ColorID::TextSelectAttentionBackground, mFrame);
      mSelectionBGColor =
          EnsureDifferentColors(mSelectionBGColor, selectionBGColor);
      break;
    }
    case nsISelectionController::SELECTION_ON: {
      mSelectionBGColor = selectionBGColor;
      break;
    }
    default: {
      mSelectionBGColor = LookAndFeel::Color(
          LookAndFeel::ColorID::TextSelectDisabledBackground, mFrame);
      mSelectionBGColor =
          EnsureDifferentColors(mSelectionBGColor, selectionBGColor);
      break;
    }
  }

  if (mResolveColors) {
    EnsureSufficientContrast(&mSelectionTextColor, &mSelectionBGColor);
  }
  return true;
}

void nsTextPaintStyle::InitTargetTextPseudoStyle() {
  if (mInitTargetTextPseudoStyle) {
    return;
  }
  mInitTargetTextPseudoStyle = true;
  mTargetTextPseudoStyle = mFrame->ComputeTargetTextStyle();
}

nsTextPaintStyle::nsSelectionStyle* nsTextPaintStyle::SelectionStyle(
    SelectionStyleIndex aIndex) {
  Maybe<nsSelectionStyle>& selectionStyle = mSelectionStyle[aIndex];
  if (!selectionStyle) {
    selectionStyle.emplace(InitSelectionStyle(aIndex));
  }
  return selectionStyle.ptr();
}

struct StyleIDs {
  LookAndFeel::ColorID mForeground, mBackground, mLine;
  LookAndFeel::IntID mLineStyle;
  LookAndFeel::FloatID mLineRelativeSize;
};
EnumeratedArray<nsTextPaintStyle::SelectionStyleIndex, StyleIDs,
                size_t(nsTextPaintStyle::SelectionStyleIndex::Count)>
    SelectionStyleIDs = {
        StyleIDs{LookAndFeel::ColorID::IMERawInputForeground,
                 LookAndFeel::ColorID::IMERawInputBackground,
                 LookAndFeel::ColorID::IMERawInputUnderline,
                 LookAndFeel::IntID::IMERawInputUnderlineStyle,
                 LookAndFeel::FloatID::IMEUnderlineRelativeSize},
        StyleIDs{LookAndFeel::ColorID::IMESelectedRawTextForeground,
                 LookAndFeel::ColorID::IMESelectedRawTextBackground,
                 LookAndFeel::ColorID::IMESelectedRawTextUnderline,
                 LookAndFeel::IntID::IMESelectedRawTextUnderlineStyle,
                 LookAndFeel::FloatID::IMEUnderlineRelativeSize},
        StyleIDs{LookAndFeel::ColorID::IMEConvertedTextForeground,
                 LookAndFeel::ColorID::IMEConvertedTextBackground,
                 LookAndFeel::ColorID::IMEConvertedTextUnderline,
                 LookAndFeel::IntID::IMEConvertedTextUnderlineStyle,
                 LookAndFeel::FloatID::IMEUnderlineRelativeSize},
        StyleIDs{LookAndFeel::ColorID::IMESelectedConvertedTextForeground,
                 LookAndFeel::ColorID::IMESelectedConvertedTextBackground,
                 LookAndFeel::ColorID::IMESelectedConvertedTextUnderline,
                 LookAndFeel::IntID::IMESelectedConvertedTextUnderline,
                 LookAndFeel::FloatID::IMEUnderlineRelativeSize},
        StyleIDs{LookAndFeel::ColorID::End, LookAndFeel::ColorID::End,
                 LookAndFeel::ColorID::SpellCheckerUnderline,
                 LookAndFeel::IntID::SpellCheckerUnderlineStyle,
                 LookAndFeel::FloatID::SpellCheckerUnderlineRelativeSize}};

nsTextPaintStyle::nsSelectionStyle nsTextPaintStyle::InitSelectionStyle(
    SelectionStyleIndex aIndex) {
  const StyleIDs& styleIDs = SelectionStyleIDs[aIndex];

  nscolor foreColor, backColor;
  if (styleIDs.mForeground == LookAndFeel::ColorID::End) {
    foreColor = NS_SAME_AS_FOREGROUND_COLOR;
  } else {
    foreColor = LookAndFeel::Color(styleIDs.mForeground, mFrame);
  }
  if (styleIDs.mBackground == LookAndFeel::ColorID::End) {
    backColor = NS_TRANSPARENT;
  } else {
    backColor = LookAndFeel::Color(styleIDs.mBackground, mFrame);
  }

  // Convert special color to actual color
  NS_ASSERTION(foreColor != NS_TRANSPARENT,
               "foreColor cannot be NS_TRANSPARENT");
  NS_ASSERTION(backColor != NS_SAME_AS_FOREGROUND_COLOR,
               "backColor cannot be NS_SAME_AS_FOREGROUND_COLOR");
  NS_ASSERTION(backColor != NS_40PERCENT_FOREGROUND_COLOR,
               "backColor cannot be NS_40PERCENT_FOREGROUND_COLOR");

  if (mResolveColors) {
    foreColor = GetResolvedForeColor(foreColor, GetTextColor(), backColor);

    if (NS_GET_A(backColor) > 0) {
      EnsureSufficientContrast(&foreColor, &backColor);
    }
  }

  nscolor lineColor;
  float relativeSize;
  StyleTextDecorationStyle lineStyle;
  GetSelectionUnderline(mFrame, aIndex, &lineColor, &relativeSize, &lineStyle);

  if (mResolveColors) {
    lineColor = GetResolvedForeColor(lineColor, foreColor, backColor);
  }

  return nsSelectionStyle{foreColor, backColor, lineColor, lineStyle,
                          relativeSize};
}

/* static */
bool nsTextPaintStyle::GetSelectionUnderline(nsIFrame* aFrame,
                                             SelectionStyleIndex aIndex,
                                             nscolor* aLineColor,
                                             float* aRelativeSize,
                                             StyleTextDecorationStyle* aStyle) {
  NS_ASSERTION(aFrame, "aFrame is null");
  NS_ASSERTION(aRelativeSize, "aRelativeSize is null");
  NS_ASSERTION(aStyle, "aStyle is null");

  const StyleIDs& styleIDs = SelectionStyleIDs[aIndex];

  nscolor color = LookAndFeel::Color(styleIDs.mLine, aFrame);
  const int32_t lineStyle = LookAndFeel::GetInt(styleIDs.mLineStyle);
  auto style = static_cast<StyleTextDecorationStyle>(lineStyle);
  if (lineStyle > static_cast<int32_t>(StyleTextDecorationStyle::Sentinel)) {
    NS_ERROR("Invalid underline style value is specified");
    style = StyleTextDecorationStyle::Solid;
  }
  float size = LookAndFeel::GetFloat(styleIDs.mLineRelativeSize);

  NS_ASSERTION(size, "selection underline relative size must be larger than 0");

  if (aLineColor) {
    *aLineColor = color;
  }
  *aRelativeSize = size;
  *aStyle = style;

  return style != StyleTextDecorationStyle::None && color != NS_TRANSPARENT &&
         size > 0.0f;
}

mozilla::Span<const StyleSimpleShadow> nsTextPaintStyle::GetSelectionShadow() {
  if (!InitSelectionColorsAndShadow()) {
    return {};
  }

  if (mSelectionPseudoStyle &&
      mSelectionPseudoStyle->HasAuthorSpecifiedTextShadow()) {
    return mSelectionPseudoStyle->StyleText()->mTextShadow.AsSpan();
  }

  return {};
}

inline nscolor Get40PercentColor(nscolor aForeColor, nscolor aBackColor) {
  nscolor foreColor = NS_RGBA(NS_GET_R(aForeColor), NS_GET_G(aForeColor),
                              NS_GET_B(aForeColor), (uint8_t)(255 * 0.4f));
  // Don't use true alpha color for readability.
  return NS_ComposeColors(aBackColor, foreColor);
}

nscolor nsTextPaintStyle::GetResolvedForeColor(nscolor aColor,
                                               nscolor aDefaultForeColor,
                                               nscolor aBackColor) {
  if (aColor == NS_SAME_AS_FOREGROUND_COLOR) {
    return aDefaultForeColor;
  }

  if (aColor != NS_40PERCENT_FOREGROUND_COLOR) {
    return aColor;
  }

  // Get actual background color
  nscolor actualBGColor = aBackColor;
  if (actualBGColor == NS_TRANSPARENT) {
    InitCommonColors();
    actualBGColor = mFrameBackgroundColor;
  }
  return Get40PercentColor(aDefaultForeColor, actualBGColor);
}

nscolor nsTextPaintStyle::GetWebkitTextStrokeColor() {
  if (mFrame->IsInSVGTextSubtree()) {
    return 0;
  }
  return mFrame->StyleText()->mWebkitTextStrokeColor.CalcColor(mFrame);
}

float nsTextPaintStyle::GetWebkitTextStrokeWidth() {
  if (mFrame->IsInSVGTextSubtree()) {
    return 0.0f;
  }
  nscoord coord = mFrame->StyleText()->mWebkitTextStrokeWidth;
  return mFrame->PresContext()->AppUnitsToFloatDevPixels(coord);
}
