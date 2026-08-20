/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * structs that contain the data provided by ComputedStyle, the
 * internal API for computed style data for an element
 */

#ifndef nsStyleStruct_h_
#define nsStyleStruct_h_

#include <cstddef>  // offsetof()

#include "CounterStyleManager.h"
#include "UniqueOrNonOwningPtr.h"
#include "X11UndefineNone.h"
#include "imgIContainer.h"
#include "imgRequestProxy.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/WindowButtonType.h"
#include "nsChangeHint.h"
#include "nsColor.h"
#include "nsCoord.h"
#include "nsFont.h"
#include "nsMargin.h"
#include "nsStyleAutoArray.h"
#include "nsStyleConsts.h"
#include "nsTArray.h"

class nsIFrame;
class nsIURI;
class nsTextFrame;
struct nsStyleDisplay;
struct nsStyleVisibility;
class nsComputedDOMStyle;
namespace mozilla {
class ComputedStyle;
struct AnchorPosResolutionCache;
class AnchorPosReferenceData;
struct IntrinsicSize;
struct SizeComputationInput;

}  // namespace mozilla

namespace mozilla::dom {
enum class CompositeOperation : uint8_t;
}  // namespace mozilla::dom

namespace mozilla {

using Position = StylePosition;

template <>
inline bool StylePosition::HasPercent() const {
  return horizontal.HasPercent() || vertical.HasPercent();
}

/**
 * True if the effective background image position described by this depends on
 * the size of the corresponding frame.
 */
template <>
inline bool StylePosition::DependsOnPositioningAreaSize() const {
  return HasPercent();
}

template <>
inline Position Position::FromPercentage(float aPercent) {
  return {LengthPercentage::FromPercentage(aPercent),
          LengthPercentage::FromPercentage(aPercent)};
}

/**
 * Convenience struct for querying if a given box has size-containment in
 * either axis.
 */
struct ContainSizeAxes {
  ContainSizeAxes(bool aIContained, bool aBContained)
      : mIContained(aIContained), mBContained(aBContained) {}

  bool IsBoth() const { return mIContained && mBContained; }
  bool IsAny() const { return mIContained || mBContained; }

  bool operator==(const ContainSizeAxes&) const = default;

  /**
   * Return a contained size from an uncontained size.
   */
  nsSize ContainSize(const nsSize& aUncontainedSize,
                     const nsIFrame& aFrame) const;
  IntrinsicSize ContainIntrinsicSize(const IntrinsicSize& aUncontainedSize,
                                     const nsIFrame& aFrame) const;
  Maybe<nscoord> ContainIntrinsicBSize(const nsIFrame& aFrame,
                                       nscoord aNoneValue = 0) const;
  Maybe<nscoord> ContainIntrinsicISize(const nsIFrame& aFrame,
                                       nscoord aNoneValue = 0) const;

  const bool mIContained;
  const bool mBContained;
};

// Used value for the CSS 'float' property (logical 'inline-*' in the computed
// value will have been resolved to 'left' or 'right').
enum class UsedFloat : uint8_t {
  None,
  Left,
  Right,
};

// Used value for the CSS 'clear' property (logical 'inline-*' in the computed
// value will have been resolved to 'left' or 'right').
enum class UsedClear : uint8_t {
  None,
  Left,
  Right,
  Both,
};

}  // namespace mozilla

#define STYLE_STRUCT(name_)                          \
  name_(const name_&);                               \
  MOZ_COUNTED_DTOR(name_);                           \
  void MarkLeaked() const { MOZ_COUNT_DTOR(name_); } \
  nsChangeHint CalcDifference(const name_&) const;

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleFont {
  STYLE_STRUCT(nsStyleFont)
  explicit nsStyleFont(const mozilla::dom::Document&);

  /**
   * Return a given size multiplied by the current text zoom factor (in
   * aPresContext).
   *
   * The size is allowed to be negative, but the caller is expected to deal with
   * negative results.
   */
  static mozilla::Length ZoomText(const mozilla::dom::Document&,
                                  mozilla::Length);

  nsAtom* GetFontPaletteAtom() const { return mFontPalette._0.AsAtom(); }

  nsFont mFont;

  // Our "computed size". Can be different from mFont.size which is our "actual
  // size" and is enforced to be >= the user's preferred min-size.  mFont.size
  // should be used for display purposes while mSize is the value to return in
  // getComputedStyle() for example.
  mozilla::NonNegativeLength mSize;

  // In stylo these three track whether the size is keyword-derived
  // and if so if it has been modified by a factor/offset
  float mFontSizeFactor;
  mozilla::Length mFontSizeOffset;
  mozilla::StyleFontSizeKeyword mFontSizeKeyword;
  mozilla::StyleFontPalette mFontPalette;
  // math-depth support (used for MathML scriptlevel)
  int8_t mMathDepth;
  mozilla::StyleLineHeight mLineHeight;
  // allow different min font-size for certain cases
  mozilla::StylePercentage mMinFontSizeRatio{1.0f};
  // MathML  mathvariant support
  mozilla::StyleMathVariant mMathVariant;
  // math-style support (used for MathML displaystyle)
  mozilla::StyleMathStyle mMathStyle;
  // math-shift support (used for MathML cramped mode)
  mozilla::StyleMathShift mMathShift;

  // Was mLanguage set based on a lang attribute in the document?
  bool mExplicitLanguage = false;

  mozilla::StyleXTextScale mXTextScale;

  bool MinFontSizeEnabled() const {
    return mXTextScale == mozilla::StyleXTextScale::All;
  }

  // The value mSize would have had if scriptminsize had never been applied
  mozilla::NonNegativeLength mScriptUnconstrainedSize;
  mozilla::Length mScriptMinSize;
  RefPtr<nsAtom> mLanguage;
};

struct nsStyleImageLayers {
  enum class LayerType : uint8_t { Background = 0, Mask };

  explicit nsStyleImageLayers(LayerType aType);
  nsStyleImageLayers(const nsStyleImageLayers& aSource);

  struct Repeat {
    mozilla::StyleImageLayerRepeat mXRepeat =
        mozilla::StyleImageLayerRepeat::Repeat;
    mozilla::StyleImageLayerRepeat mYRepeat =
        mozilla::StyleImageLayerRepeat::Repeat;

    // Initialize nothing
    Repeat() = default;

    bool IsInitialValue() const {
      return mXRepeat == mozilla::StyleImageLayerRepeat::Repeat &&
             mYRepeat == mozilla::StyleImageLayerRepeat::Repeat;
    }

    bool DependsOnPositioningAreaSize() const {
      return mXRepeat == mozilla::StyleImageLayerRepeat::Space ||
             mYRepeat == mozilla::StyleImageLayerRepeat::Space;
    }

    bool operator==(const Repeat& aOther) const = default;
  };

  struct Layer {
    using StyleGeometryBox = mozilla::StyleGeometryBox;
    using StyleBackgroundClip = mozilla::StyleBackgroundClip;
    using StyleImageLayerAttachment = mozilla::StyleImageLayerAttachment;
    using StyleBackgroundSize = mozilla::StyleBackgroundSize;

    mozilla::StyleImage mImage;
    mozilla::Position mPosition;
    StyleBackgroundSize mSize;
    StyleBackgroundClip mClip;
    MOZ_INIT_OUTSIDE_CTOR StyleGeometryBox mOrigin;

    // This property is used for background layer only.
    // For a mask layer, it should always be the initial value, which is
    // StyleImageLayerAttachment::Scroll.
    StyleImageLayerAttachment mAttachment;

    // This property is used for background layer only.
    // For a mask layer, it should always be the initial value, which is
    // StyleBlend::Normal.
    mozilla::StyleBlend mBlendMode;

    // This property is used for mask layer only.
    // For a background layer, it should always be the initial value, which is
    // StyleMaskComposite::Add.
    mozilla::StyleMaskComposite mComposite;

    // mask-only property. This property is used for mask layer only. For a
    // background layer, it should always be the initial value, which is
    // StyleMaskMode::MatchSource.
    mozilla::StyleMaskMode mMaskMode;

    Repeat mRepeat;

    // This constructor does not initialize mRepeat or mOrigin and Initialize()
    // must be called to do that.
    Layer();
    ~Layer();

    // Initialize mRepeat and mOrigin by specified layer type
    void Initialize(LayerType aType);

    void ResolveImage(mozilla::dom::Document& aDocument,
                      const Layer* aOldLayer) {
      mImage.ResolveImage(aDocument, aOldLayer ? &aOldLayer->mImage : nullptr);
    }

    // True if the rendering of this layer might change when the size
    // of the background positioning area changes.  This is true for any
    // non-solid-color background whose position or size depends on
    // the size of the positioning area.  It's also true for SVG images
    // whose root <svg> node has a viewBox.
    bool RenderingMightDependOnPositioningAreaSizeChange() const;

    // Compute the change hint required by changes in just this layer.
    nsChangeHint CalcDifference(const Layer& aNewLayer) const;

    bool operator==(const Layer&) const = default;
  };

  // The (positive) number of computed values of each property, since
  // the lengths of the lists are independent.
  uint32_t mAttachmentCount;
  uint32_t mClipCount;
  uint32_t mOriginCount;
  uint32_t mRepeatCount;
  uint32_t mPositionXCount;
  uint32_t mPositionYCount;
  uint32_t mImageCount;
  uint32_t mSizeCount;
  uint32_t mMaskModeCount;
  uint32_t mBlendModeCount;
  uint32_t mCompositeCount;

  // Layers are stored in an array, matching the top-to-bottom order in
  // which they are specified in CSS.  The number of layers to be used
  // should come from the background-image property.  We create
  // additional |Layer| objects for *any* property, not just
  // background-image.  This means that the bottommost layer that
  // callers in layout care about (which is also the one whose
  // background-clip applies to the background-color) may not be last
  // layer.  In layers below the bottom layer, properties will be
  // uninitialized unless their count, above, indicates that they are
  // present.
  nsStyleAutoArray<Layer> mLayers;

  const Layer& BottomLayer() const { return mLayers[mImageCount - 1]; }

  void ResolveImages(mozilla::dom::Document& aDocument,
                     const nsStyleImageLayers* aOldLayers) {
    for (uint32_t i = 0; i < mImageCount; ++i) {
      const Layer* oldLayer = (aOldLayers && aOldLayers->mLayers.Length() > i)
                                  ? &aOldLayers->mLayers[i]
                                  : nullptr;
      mLayers[i].ResolveImage(aDocument, oldLayer);
    }
  }

  // Fill unspecified layers by cycling through their values
  // till they all are of length aMaxItemCount
  void FillAllLayers(uint32_t aMaxItemCount);

  nsChangeHint CalcDifference(const nsStyleImageLayers& aNewLayers,
                              nsStyleImageLayers::LayerType aType) const;

  nsStyleImageLayers& operator=(const nsStyleImageLayers& aOther);
  nsStyleImageLayers& operator=(nsStyleImageLayers&& aOther) = default;
  bool operator==(const nsStyleImageLayers& aOther) const;

  static const NonCustomCSSPropertyId kBackgroundLayerTable[];
  static const NonCustomCSSPropertyId kMaskLayerTable[];

#define NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(var_, layers_) \
  for (uint32_t var_ = (layers_).mImageCount; (var_)-- != 0;)
#define NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT_WITH_RANGE(var_, layers_,  \
                                                             start_, count_) \
  NS_ASSERTION(                                                              \
      (int32_t)(start_) >= 0 && (uint32_t)(start_) < (layers_).mImageCount,  \
      "Invalid layer start!");                                               \
  NS_ASSERTION((count_) > 0 && (count_) <= (start_) + 1,                     \
               "Invalid layer range!");                                      \
  for (uint32_t var_ = (start_) + 1;                                         \
       (var_)-- != (uint32_t)((start_) + 1 - (count_));)
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleBackground {
  STYLE_STRUCT(nsStyleBackground)
  nsStyleBackground();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleBackground*);

  // Return the background color as nscolor.
  nscolor BackgroundColor(const nsIFrame* aFrame) const;
  nscolor BackgroundColor(const mozilla::ComputedStyle* aStyle) const;

  // True if this background is completely transparent.
  bool IsTransparent(const nsIFrame* aFrame) const;
  bool IsTransparent(const mozilla::ComputedStyle* aStyle) const;

  // We have to take slower codepaths for fixed background attachment,
  // but we don't want to do that when there's no image.
  // Not inline because it uses an nsCOMPtr<imgIRequest>
  // FIXME: Should be in nsStyleStructInlines.h.
  bool HasFixedBackground(nsIFrame* aFrame) const;

  // Checks to see if this has a non-empty image with "local" attachment.
  // This is defined in nsStyleStructInlines.h.
  inline bool HasLocalBackground() const;

  const nsStyleImageLayers::Layer& BottomLayer() const {
    return mImage.BottomLayer();
  }

  nsStyleImageLayers mImage;
  mozilla::StyleColor mBackgroundColor;
};

using AnchorResolvedMargin =
    mozilla::UniqueOrNonOwningPtr<const mozilla::StyleMargin>;

// Base set of parameters required to resolve a reference to an anchor.
struct AnchorPosResolutionParams {
  struct AutoResolutionOverrideParams {
    // Whether anchor-center is being used on the horizontal axis.
    bool mHAnchorCenter = false;
    // Whether anchor-center is being used on the vertical axis.
    bool mVAnchorCenter = false;
    // Whether position-area is being used.
    bool mPositionAreaInUse = false;

    AutoResolutionOverrideParams() = default;
    AutoResolutionOverrideParams(
        const nsIFrame* aFrame,
        const mozilla::AnchorPosResolutionCache* aCache);
    explicit AutoResolutionOverrideParams(const nsIFrame* aFrame);

    bool OverriddenToZero(mozilla::StylePhysicalAxis aAxis) const;
  };
  // Frame of the anchor positioned element.
  // If nullptr, skips anchor lookup and returns invalid, resolving fallbacks.
  const nsIFrame* mFrame;
  // Position property of the element in question.
  mozilla::StylePositionProperty mPosition;
  // Cache data used for anchor resolution.
  mozilla::AnchorPosResolutionCache* const mCache;
  // Set of parameters that override `auto` values to 0, if the default
  // anchor is valid.
  AutoResolutionOverrideParams mAutoResolutionOverrideParams;

  // Helper functions for creating anchor resolution parameters.
  // Defined in corresponding header files.
  static inline AnchorPosResolutionParams From(
      const nsIFrame* aFrame,
      mozilla::AnchorPosResolutionCache* aAnchorPosResolutionCache = nullptr);
  static AnchorPosResolutionParams From(
      const mozilla::SizeComputationInput* aSizingInput,
      bool aIgnorePositionArea = false);
  static inline AnchorPosResolutionParams From(
      const nsComputedDOMStyle* aComputedDOMStyle);
};

struct AnchorResolvedMarginHelper {
  static const mozilla::StyleMargin& ZeroValue() {
    static const auto value = mozilla::StyleMargin::LengthPercentage(
        mozilla::StyleLengthPercentage::Zero());
    return value;
  }

  static AnchorResolvedMargin FromUnresolved(
      const mozilla::StyleMargin& aValue, mozilla::StylePhysicalAxis aAxis,
      const AnchorPosResolutionParams& aParams) {
    auto resolved = [&]() {
      if (aValue.HasAnchorPositioningFunction()) {
        return ResolveAnchor(aValue, aAxis, aParams);
      }
      return AnchorResolvedMargin::NonOwning(&aValue);
    }();
    if (resolved->IsAuto() &&
        aParams.mAutoResolutionOverrideParams.OverriddenToZero(aAxis)) {
      return Zero();
    }
    return resolved;
  }

 private:
  static AnchorResolvedMargin Zero() {
    return AnchorResolvedMargin::NonOwning(&ZeroValue());
  }

  static AnchorResolvedMargin ResolveAnchor(
      const mozilla::StyleMargin& aValue, mozilla::StylePhysicalAxis aAxis,
      const AnchorPosResolutionParams& aParams);
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleMargin {
  STYLE_STRUCT(nsStyleMargin)
  nsStyleMargin();

  // Returns false if any margin is layout-dependent in any way.
  // Percentage values and/or `anchor-size()` will do this.
  bool GetMargin(nsMargin& aMargin) const {
    bool convertsToLength = mMargin.All(
        [](const auto& aLength) { return aLength.ConvertsToLength(); });

    if (!convertsToLength) {
      return false;
    }

    for (const auto side : mozilla::AllPhysicalSides()) {
      aMargin.Side(side) = mMargin.Get(side).AsLengthPercentage().ToLength();
    }
    return true;
  }

  nsMargin GetScrollMargin() const {
    return nsMargin(mScrollMargin.Get(mozilla::eSideTop).ToAppUnits(),
                    mScrollMargin.Get(mozilla::eSideRight).ToAppUnits(),
                    mScrollMargin.Get(mozilla::eSideBottom).ToAppUnits(),
                    mScrollMargin.Get(mozilla::eSideLeft).ToAppUnits());
  }

  // Return true if either the start or end side in the axis is 'auto'.
  // (defined in WritingModes.h since we need the full WritingMode type)
  inline bool HasBlockAxisAuto(mozilla::WritingMode aWM,
                               const AnchorPosResolutionParams& aParams) const;
  inline bool HasInlineAxisAuto(mozilla::WritingMode aWM,
                                const AnchorPosResolutionParams& aParams) const;
  inline bool HasAuto(mozilla::LogicalAxis, mozilla::WritingMode,
                      const AnchorPosResolutionParams& aParams) const;

  // Attempt to return the resolved margin, resolving anchor functions, and
  // using a dummy percentage basis. If the resulting value returns true for
  // `HasPercent`, percentage value needs to be resolved with a proper basis at
  // a later point.
  AnchorResolvedMargin GetMargin(
      mozilla::Side aSide, const AnchorPosResolutionParams& aParams) const {
    return AnchorResolvedMarginHelper::FromUnresolved(
        mMargin.Get(aSide), mozilla::ToStylePhysicalAxis(aSide), aParams);
  }

  bool MarginEquals(const nsStyleMargin& aOther) const {
    for (const auto side : mozilla::AllPhysicalSides()) {
      if (mMargin.Get(side) != aOther.mMargin.Get(side)) {
        return false;
      }
    }
    return true;
  }

  // As with other logical-coordinate accessors, definitions for these
  // are found in WritingModes.h.
  inline AnchorResolvedMargin GetMargin(
      mozilla::LogicalSide aSide, mozilla::WritingMode aWM,
      const AnchorPosResolutionParams& aParams) const;

  mozilla::StyleRect<mozilla::StyleMargin> mMargin;
  mozilla::StyleRect<mozilla::StyleLength> mScrollMargin;
  // TODO: Add support per-axis/side clipping, see
  // https://github.com/w3c/csswg-drafts/issues/7245
  mozilla::StyleOverflowClipMargin mOverflowClipMargin;

  mozilla::StyleMarginTrim mMarginTrim;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStylePadding {
  STYLE_STRUCT(nsStylePadding)
  nsStylePadding();

  mozilla::StyleRect<mozilla::NonNegativeLengthPercentage> mPadding;
  mozilla::StyleRect<mozilla::NonNegativeLengthPercentageOrAuto> mScrollPadding;

  inline bool IsWidthDependent() const {
    return !mPadding.All(
        [](const auto& aLength) { return aLength.ConvertsToLength(); });
  }

  bool GetPadding(nsMargin& aPadding) const {
    if (IsWidthDependent()) {
      return false;
    }

    for (const auto side : mozilla::AllPhysicalSides()) {
      // Clamp negative calc() to 0.
      aPadding.Side(side) = std::max(mPadding.Get(side).ToLength(), 0);
    }
    return true;
  }
};

// Border widths are rounded to the nearest-below integer number of pixels,
// but values between zero and one device pixels are always rounded up to
// one device pixel.
#define NS_ROUND_BORDER_TO_PIXELS(l, tpp) \
  ((l) == 0) ? 0 : std::max((tpp), (l) / (tpp) * (tpp))

// Returns if the given border style type is visible or not
static inline bool IsVisibleBorderStyle(mozilla::StyleBorderStyle aStyle) {
  return aStyle != mozilla::StyleBorderStyle::None &&
         aStyle != mozilla::StyleBorderStyle::Hidden;
}

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleBorder {
  STYLE_STRUCT(nsStyleBorder)
  nsStyleBorder();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleBorder*);

  // Return whether aStyle is a visible style.  Invisible styles cause
  // the relevant computed border width to be 0.
  // Note that this does *not* consider the effects of 'border-image':
  // if border-style is none, but there is a loaded border image,
  // HasVisibleStyle will be false even though there *is* a border.
  bool HasVisibleStyle(mozilla::Side aSide) const {
    return IsVisibleBorderStyle(mBorderStyle.Get(aSide));
  }

  // aBorderWidth is in twips
  void SetBorderWidth(mozilla::Side aSide, nscoord aBorderWidth,
                      nscoord aAppUnitsPerDevPixel) {
    mBorder.Get(aSide) =
        NS_ROUND_BORDER_TO_PIXELS(aBorderWidth, aAppUnitsPerDevPixel);
  }

  // Get the computed border (plus rounding).
  nsMargin GetComputedBorder() const {
    nsMargin border(mBorder._0, mBorder._1, mBorder._2, mBorder._3);
    for (auto side : mozilla::AllPhysicalSides()) {
      if (!HasVisibleStyle(side)) {
        border.Side(side) = 0;
      }
    }
    return border;
  }

  bool HasBorder() const {
    for (auto side : mozilla::AllPhysicalSides()) {
      if (mBorder.Get(side) && HasVisibleStyle(side)) {
        return true;
      }
    }
    return !mBorderImageSource.IsNone();
  }

  // Get the actual border width for a particular side, in appunits.  Note that
  // this is zero if and only if there is no border to be painted for this
  // side.  That is, this value takes into account the border style and the
  // value is rounded to the nearest device pixel by NS_ROUND_BORDER_TO_PIXELS.
  nscoord GetComputedBorderWidth(mozilla::Side aSide) const {
    if (!HasVisibleStyle(aSide)) {
      return 0;
    }
    return mBorder.Get(aSide);
  }

  mozilla::StyleBorderStyle GetBorderStyle(mozilla::Side aSide) const {
    NS_ASSERTION(aSide <= mozilla::eSideLeft, "bad side");
    return mBorderStyle.Get(aSide);
  }

  void SetBorderStyle(mozilla::Side aSide, mozilla::StyleBorderStyle aStyle) {
    NS_ASSERTION(aSide <= mozilla::eSideLeft, "bad side");
    mBorderStyle.Get(aSide) = aStyle;
  }

  inline bool IsBorderImageSizeAvailable() const {
    return mBorderImageSource.IsSizeAvailable();
  }

  nsMargin GetImageOutset() const;

  imgIRequest* GetBorderImageRequest() const {
    return mBorderImageSource.GetImageRequest();
  }

 public:
  mozilla::StyleBorderRadius mBorderRadius;  // coord, percent
  mozilla::StyleCornerShapeRect mCornerShape;
  mozilla::StyleImage mBorderImageSource;
  mozilla::StyleBorderImageWidth mBorderImageWidth;
  mozilla::StyleNonNegativeLengthOrNumberRect mBorderImageOutset;
  mozilla::StyleBorderImageSlice mBorderImageSlice;  // factor, percent
  mozilla::StyleBorderImageRepeat mBorderImageRepeat;
  mozilla::StyleFloatEdge mFloatEdge;
  mozilla::StyleBoxDecorationBreak mBoxDecorationBreak;

 protected:
  mozilla::StyleRect<mozilla::StyleBorderStyle> mBorderStyle;
  mozilla::StyleRect<mozilla::StyleBorderSideWidth> mBorder;

 public:
  // the colors to use for a simple border.
  // not used for -moz-border-colors
  mozilla::StyleColor mBorderTopColor;
  mozilla::StyleColor mBorderRightColor;
  mozilla::StyleColor mBorderBottomColor;
  mozilla::StyleColor mBorderLeftColor;

  mozilla::StyleColor& BorderColorFor(mozilla::Side aSide) {
    switch (aSide) {
      case mozilla::eSideTop:
        return mBorderTopColor;
      case mozilla::eSideRight:
        return mBorderRightColor;
      case mozilla::eSideBottom:
        return mBorderBottomColor;
      case mozilla::eSideLeft:
        return mBorderLeftColor;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown side");
    return mBorderTopColor;
  }

  const mozilla::StyleColor& BorderColorFor(mozilla::Side aSide) const {
    switch (aSide) {
      case mozilla::eSideTop:
        return mBorderTopColor;
      case mozilla::eSideRight:
        return mBorderRightColor;
      case mozilla::eSideBottom:
        return mBorderBottomColor;
      case mozilla::eSideLeft:
        return mBorderLeftColor;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown side");
    return mBorderTopColor;
  }

  static mozilla::StyleColor nsStyleBorder::* BorderColorFieldFor(
      mozilla::Side aSide) {
    switch (aSide) {
      case mozilla::eSideTop:
        return &nsStyleBorder::mBorderTopColor;
      case mozilla::eSideRight:
        return &nsStyleBorder::mBorderRightColor;
      case mozilla::eSideBottom:
        return &nsStyleBorder::mBorderBottomColor;
      case mozilla::eSideLeft:
        return &nsStyleBorder::mBorderLeftColor;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown side");
    return nullptr;
  }

  nsStyleBorder& operator=(const nsStyleBorder&) = delete;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleOutline {
  STYLE_STRUCT(nsStyleOutline)
  nsStyleOutline();

  mozilla::StyleBorderSideWidth mOutlineWidth;
  mozilla::StyleAu mOutlineOffset;
  mozilla::StyleColor mOutlineColor;
  mozilla::StyleOutlineStyle mOutlineStyle;

  bool ShouldPaintOutline() const {
    if (mOutlineStyle.IsAuto()) {
      return true;
    }
    if (!IsVisibleBorderStyle(mOutlineStyle.AsBorderStyle())) {
      return false;
    }
    return mOutlineWidth > 0;
  }

  nsSize EffectiveOffsetFor(const nsRect& aRect) const;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleList {
  STYLE_STRUCT(nsStyleList)
  nsStyleList();

  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleList*);

  nsStyleList& operator=(const nsStyleList& aOther) = delete;
  nsChangeHint CalcDifference(const nsStyleList& aNewData,
                              const mozilla::ComputedStyle& aOldStyle) const;

  mozilla::StyleListStylePosition mListStylePosition;

  mozilla::StyleListStyleType mListStyleType;
  mozilla::StyleQuotes mQuotes;
  mozilla::StyleImage mListStyleImage;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStylePage {
  STYLE_STRUCT(nsStylePage)
  MOZ_COUNTED_DEFAULT_CTOR(nsStylePage)

  using StylePageOrientation = mozilla::StylePageOrientation;
  using StylePageSize = mozilla::StylePageSize;
  using StylePageName = mozilla::StylePageName;

  // page-size property.
  StylePageSize mSize = StylePageSize::Auto();
  // page-name property.
  StylePageName mPage = StylePageName::Auto();
  // page-orientation property.
  StylePageOrientation mPageOrientation = StylePageOrientation::Upright;
};

using AnchorResolvedInset =
    mozilla::UniqueOrNonOwningPtr<const mozilla::StyleInset>;

// Set of parameters required to resolve the anchor's position offset in a
// containing block.
struct AnchorPosOffsetResolutionParams {
  // Size of the containing block for the anchor positioned element.
  // This needs to be set only if all of the following conditions are true:
  //
  //   * The resolution is happening during reflow (i.e. Containing block
  //   doesn't have its rect set)
  //   * The caller needs the correct size, not just its type (e.g. Just
  //     checking `HasPercent()` of the inset resolved value)
  const mozilla::LogicalSize* mCBSize;
  AnchorPosResolutionParams mBaseParams;

  static AnchorPosOffsetResolutionParams UseCBFrameSize(
      const AnchorPosResolutionParams& aBaseParams) {
    return {aBaseParams, nullptr};
  }

  static AnchorPosOffsetResolutionParams ExplicitCBFrameSize(
      const AnchorPosResolutionParams& aBaseParams,
      const mozilla::LogicalSize* aCBSize) {
    return {aBaseParams, aCBSize};
  }

 private:
  AnchorPosOffsetResolutionParams(const AnchorPosResolutionParams& aBaseParams,
                                  const mozilla::LogicalSize* aCBSize)
      : mCBSize{aCBSize}, mBaseParams{aBaseParams} {}
};

struct AnchorResolvedInsetHelper {
  static const mozilla::StyleInset& AutoValue() {
    static const auto value = mozilla::StyleInset::Auto();
    return value;
  }

  static const mozilla::StyleInset& ZeroValue() {
    static const auto value = mozilla::StyleInset::LengthPercentage(
        mozilla::StyleLengthPercentage::Zero());
    return value;
  }

  static AnchorResolvedInset FromUnresolved(
      const mozilla::StyleInset& aValue, mozilla::Side aSide,
      const AnchorPosOffsetResolutionParams& aParams) {
    auto resolved = [&]() {
      if (aValue.HasAnchorPositioningFunction()) {
        return ResolveAnchor(aValue, mozilla::ToStylePhysicalSide(aSide),
                             aParams);
      }
      return AnchorResolvedInset::NonOwning(&aValue);
    }();
    if (resolved->IsAuto() &&
        aParams.mBaseParams.mAutoResolutionOverrideParams.OverriddenToZero(
            mozilla::ToStylePhysicalAxis(aSide))) {
      return AnchorResolvedInset::NonOwning(&ZeroValue());
    }
    return resolved;
  }

 private:
  static AnchorResolvedInset Auto() {
    return AnchorResolvedInset::NonOwning(&AutoValue());
  }

  static AnchorResolvedInset ResolveAnchor(
      const mozilla::StyleInset& aValue, mozilla::StylePhysicalSide aSide,
      const AnchorPosOffsetResolutionParams& aParams);
};

using AnchorResolvedSize =
    mozilla::UniqueOrNonOwningPtr<const mozilla::StyleSize>;

struct AnchorResolvedSizeHelper {
  static const mozilla::StyleSize& ZeroValue() {
    static const auto value = mozilla::StyleSize::LengthPercentage(
        mozilla::StyleLengthPercentage::Zero());
    return value;
  }

  static const mozilla::StyleSize& MinContentValue() {
    static const auto value = mozilla::StyleSize::MinContent();
    return value;
  }

  static const mozilla::StyleSize& AutoValue() {
    static const auto value = mozilla::StyleSize::Auto();
    return value;
  }

  static AnchorResolvedSize FromUnresolved(
      const mozilla::StyleSize& aValue, mozilla::StylePhysicalAxis aAxis,
      const AnchorPosResolutionParams& aParams) {
    if (aValue.HasAnchorPositioningFunction()) {
      return ResolveAnchor(aValue, aAxis, aParams);
    }
    return AnchorResolvedSize::NonOwning(&aValue);
  }

  static AnchorResolvedSize Overridden(const mozilla::StyleSize& aSize) {
    return AnchorResolvedSize::NonOwning(&aSize);
  }

  static AnchorResolvedSize Zero() {
    return AnchorResolvedSize::NonOwning(&ZeroValue());
  }

  static AnchorResolvedSize MinContent() {
    return AnchorResolvedSize::NonOwning(&MinContentValue());
  }

  static AnchorResolvedSize Auto() {
    return AnchorResolvedSize::NonOwning(&AutoValue());
  }

  static AnchorResolvedSize LengthPercentage(
      const mozilla::StyleLengthPercentage& aLP) {
    return mozilla::MakeUniqueOfUniqueOrNonOwning<const mozilla::StyleSize>(
        aLP);
  }

 private:
  static AnchorResolvedSize ResolveAnchor(
      const mozilla::StyleSize& aValue, mozilla::StylePhysicalAxis aAxis,
      const AnchorPosResolutionParams& aParams);
};

using AnchorResolvedMaxSize =
    mozilla::UniqueOrNonOwningPtr<const mozilla::StyleMaxSize>;

struct AnchorResolvedMaxSizeHelper {
  static const mozilla::StyleMaxSize& MaxContentValue() {
    static const auto value = mozilla::StyleMaxSize::MaxContent();
    return value;
  }
  static const mozilla::StyleMaxSize& NoneValue() {
    static const auto value = mozilla::StyleMaxSize::None();
    return value;
  }

  static AnchorResolvedMaxSize FromUnresolved(
      const mozilla::StyleMaxSize& aValue, mozilla::StylePhysicalAxis aAxis,
      const AnchorPosResolutionParams& aParams) {
    if (aValue.HasAnchorPositioningFunction()) {
      return ResolveAnchor(aValue, aAxis, aParams);
    }
    return AnchorResolvedMaxSize::NonOwning(&aValue);
  }
  static AnchorResolvedMaxSize MaxContent() {
    return AnchorResolvedMaxSize::NonOwning(&MaxContentValue());
  }

  static AnchorResolvedMaxSize None() {
    return AnchorResolvedMaxSize::NonOwning(&NoneValue());
  }

 private:
  static AnchorResolvedMaxSize ResolveAnchor(
      const mozilla::StyleMaxSize& aValue, mozilla::StylePhysicalAxis aAxis,
      const AnchorPosResolutionParams& aParams);
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStylePosition {
  STYLE_STRUCT(nsStylePosition)
  nsStylePosition();

  using LengthPercentageOrAuto = mozilla::LengthPercentageOrAuto;
  using Position = mozilla::Position;
  template <typename T>
  using StyleRect = mozilla::StyleRect<T>;
  using StyleSize = mozilla::StyleSize;
  using StyleMaxSize = mozilla::StyleMaxSize;
  using WritingMode = mozilla::WritingMode;
  using LogicalAxis = mozilla::LogicalAxis;
  using StyleImplicitGridTracks = mozilla::StyleImplicitGridTracks;
  using ComputedStyle = mozilla::ComputedStyle;
  using StyleSelfAlignment = mozilla::StyleSelfAlignment;

  nsChangeHint CalcDifference(const nsStylePosition& aNewData,
                              const ComputedStyle& aOldStyle) const;

  // Returns whether we need to compute an hypothetical position if we were
  // absolutely positioned.
  bool NeedsHypotheticalPositionIfAbsPos() const {
    // NOTE(dshin): By passing in nullptr for frames, we're guaranteeing that
    // no anchor is found, instead considering fallbacks or anchor-invalid
    // values. Since anchor resolved value can never be auto, this is
    // guaranteed to be correct, if not pessimistic.
    // TODO(dshin): Probably nicer if we try to resolve. Two tricky parts:
    //   * `CalcDifference`: Need to pass in the changing frame somehow.
    //   * Reflow cases where containing block is not yet set (Mainly when
    //     `nsBlockFrame` is about to run another reflow for clearance)
    const auto anchorResolutionParams =
        AnchorPosOffsetResolutionParams::UseCBFrameSize(
            {nullptr, mozilla::StylePositionProperty::Absolute});
    return (GetAnchorResolvedInset(mozilla::eSideRight, anchorResolutionParams)
                ->IsAuto() &&
            GetAnchorResolvedInset(mozilla::eSideLeft, anchorResolutionParams)
                ->IsAuto()) ||
           (GetAnchorResolvedInset(mozilla::eSideTop, anchorResolutionParams)
                ->IsAuto() &&
            GetAnchorResolvedInset(mozilla::eSideBottom, anchorResolutionParams)
                ->IsAuto());
  }

  const mozilla::StyleContainIntrinsicSize& ContainIntrinsicBSize(
      const WritingMode& aWM) const;
  const mozilla::StyleContainIntrinsicSize& ContainIntrinsicISize(
      const WritingMode& aWM) const;

  /**
   * Return the used value for 'align-self' given our parent ComputedStyle
   * (or null for the root).
   */
  StyleSelfAlignment UsedAlignSelf(const ComputedStyle*) const;

  /**
   * Return the used value for 'justify-self' given our parent ComputedStyle
   * aParent (or null for the root).
   */
  StyleSelfAlignment UsedJustifySelf(const ComputedStyle*) const;

  /**
   * Return the used value for 'justify/align-self' for the axis in the
   * alignment container's writing mode.
   *
   * @param aAlignContainerStyle the computed style of the alignment container,
   *        or null for the root.
   *
   * (defined in WritingModes.h since we need the full WritingMode type)
   */
  inline mozilla::StyleAlignFlags UsedSelfAlignment(
      LogicalAxis aAlignContainerAxis,
      const mozilla::ComputedStyle* aAlignContainerStyle) const;

  /**
   * Return the used value for 'justify/align-self' for the axis in the
   * alignment subject's writing mode.
   *
   * @param aAlignSubjectAxis the axis in aAlignSubjectWM.
   * @param aAlignContainerStyle the computed style of the alignment container,
   *        or null for the root.
   *
   * (defined in WritingModes.h since we need the full WritingMode type)
   */
  inline mozilla::StyleAlignFlags UsedSelfAlignment(
      WritingMode aAlignSubjectWM, LogicalAxis aAlignSubjectAxis,
      WritingMode aAlignContainerWM,
      const ComputedStyle* aAlignContainerStyle) const;

  /**
   * Return the used value for 'justify/align-content' in aAxis.
   * (defined in WritingModes.h since we need the full WritingMode type)
   */
  inline mozilla::StyleContentDistribution UsedContentAlignment(
      LogicalAxis aAxis) const;

  bool CanHaveDefaultAnchor() const {
    return mPositionAnchor.value.IsIdent() || mPositionAnchor.value.IsAuto() ||
           (mPositionAnchor.value.IsNormal() && !mPositionArea.IsNone());
  }

  Position mObjectPosition;
  StyleRect<mozilla::StyleInset> mOffset;
  StyleSize mWidth;
  StyleSize mMinWidth;
  StyleMaxSize mMaxWidth;
  StyleSize mHeight;
  StyleSize mMinHeight;
  StyleMaxSize mMaxHeight;

  // 'auto' or a `<dashed-ident>` referencing an anchor positioning anchor
  // element.
  mozilla::StylePositionAnchor mPositionAnchor;
  mozilla::StylePositionArea mPositionArea;
  mozilla::StylePositionVisibility mPositionVisibility;
  mozilla::StylePositionTryFallbacks mPositionTryFallbacks;
  mozilla::StylePositionTryOrder mPositionTryOrder;

  mozilla::StyleFlexBasis mFlexBasis;
  StyleImplicitGridTracks mGridAutoColumns;
  StyleImplicitGridTracks mGridAutoRows;
  mozilla::StyleAspectRatio mAspectRatio;
  mozilla::StyleGridAutoFlow mGridAutoFlow;
  mozilla::StyleMasonryAutoFlow mMasonryAutoFlow;

  mozilla::StyleContentDistribution mAlignContent;
  mozilla::StyleItemPlacement mAlignItems;
  mozilla::StyleSelfAlignment mAlignSelf;
  mozilla::StyleContentDistribution mJustifyContent;
  mozilla::StyleComputedJustifyItems mJustifyItems;
  mozilla::StyleSelfAlignment mJustifySelf;
  mozilla::StyleFlexDirection mFlexDirection;
  mozilla::StyleFlexWrap mFlexWrap;
  mozilla::StyleObjectFit mObjectFit;
  mozilla::StyleBoxSizing mBoxSizing;
  int32_t mOrder;
  float mFlexGrow;
  float mFlexShrink;
  mozilla::StyleZIndex mZIndex;

  mozilla::StyleGridTemplateComponent mGridTemplateColumns;
  mozilla::StyleGridTemplateComponent mGridTemplateRows;
  mozilla::StyleGridTemplateAreas mGridTemplateAreas;

  mozilla::StyleGridLine mGridColumnStart;
  mozilla::StyleGridLine mGridColumnEnd;
  mozilla::StyleGridLine mGridRowStart;
  mozilla::StyleGridLine mGridRowEnd;
  mozilla::NonNegativeLengthPercentageOrNormal mColumnGap;
  mozilla::NonNegativeLengthPercentageOrNormal mRowGap;

  mozilla::StyleContainIntrinsicSize mContainIntrinsicWidth;
  mozilla::StyleContainIntrinsicSize mContainIntrinsicHeight;

  // Logical-coordinate accessors for width and height properties,
  // given a WritingMode value. The definitions of these methods are
  // found in WritingModes.h (after the WritingMode class is fully
  // declared).
  inline AnchorResolvedSize ISize(WritingMode,
                                  const AnchorPosResolutionParams&) const;
  inline AnchorResolvedSize MinISize(WritingMode,
                                     const AnchorPosResolutionParams&) const;
  inline AnchorResolvedMaxSize MaxISize(WritingMode,
                                        const AnchorPosResolutionParams&) const;
  inline AnchorResolvedSize BSize(WritingMode,
                                  const AnchorPosResolutionParams&) const;
  inline AnchorResolvedSize MinBSize(WritingMode,
                                     const AnchorPosResolutionParams&) const;
  inline AnchorResolvedMaxSize MaxBSize(WritingMode,
                                        const AnchorPosResolutionParams&) const;
  inline AnchorResolvedSize Size(LogicalAxis, WritingMode,
                                 const AnchorPosResolutionParams&) const;
  inline AnchorResolvedSize MinSize(LogicalAxis, WritingMode,
                                    const AnchorPosResolutionParams&) const;
  inline AnchorResolvedMaxSize MaxSize(LogicalAxis, WritingMode,
                                       const AnchorPosResolutionParams&) const;
  static inline bool ISizeDependsOnContainer(const AnchorResolvedSize&);
  static inline bool MinISizeDependsOnContainer(const AnchorResolvedSize&);
  static inline bool MaxISizeDependsOnContainer(const AnchorResolvedMaxSize&);
  static inline bool BSizeDependsOnContainer(const AnchorResolvedSize&);
  static inline bool MinBSizeDependsOnContainer(const AnchorResolvedSize&);
  static inline bool MaxBSizeDependsOnContainer(const AnchorResolvedMaxSize&);

  // TODO(dshin): These inset getters are to be removed when
  // interleaving computation is implemented.
  AnchorResolvedInset GetAnchorResolvedInset(
      mozilla::Side aSide,
      const AnchorPosOffsetResolutionParams& aParams) const {
    return AnchorResolvedInsetHelper::FromUnresolved(mOffset.Get(aSide), aSide,
                                                     aParams);
  }

  inline AnchorResolvedInset GetAnchorResolvedInset(
      mozilla::LogicalSide aSide, WritingMode aWM,
      const AnchorPosOffsetResolutionParams& aParams) const;

  // Returns the side with an auto inset if exactly one inset in the given
  // physical axis is auto. Otherwise returns Nothing().
  mozilla::Maybe<mozilla::Side> GetSingleAutoInsetInAxis(
      mozilla::StylePhysicalAxis aAxis,
      const AnchorPosOffsetResolutionParams& aParams) const {
    const mozilla::Side startSide =
        aAxis == mozilla::StylePhysicalAxis::Horizontal ? mozilla::eSideLeft
                                                        : mozilla::eSideTop;
    const mozilla::Side endSide =
        aAxis == mozilla::StylePhysicalAxis::Horizontal ? mozilla::eSideRight
                                                        : mozilla::eSideBottom;

    const bool startInsetIsAuto =
        AnchorResolvedInsetHelper::FromUnresolved(mOffset.Get(startSide),
                                                  startSide, aParams)
            ->IsAuto();
    const bool endInsetIsAuto = AnchorResolvedInsetHelper::FromUnresolved(
                                    mOffset.Get(endSide), endSide, aParams)
                                    ->IsAuto();

    if (startInsetIsAuto && !endInsetIsAuto) {
      return mozilla::Some(startSide);
    }
    if (!startInsetIsAuto && endInsetIsAuto) {
      return mozilla::Some(endSide);
    }
    return mozilla::Nothing();
  }

  // Logical-axis version, defined in WritingModes.h
  inline mozilla::Maybe<mozilla::Side> GetSingleAutoInsetInAxis(
      LogicalAxis aAxis, WritingMode aWM,
      const AnchorPosOffsetResolutionParams& aParams) const;

  AnchorResolvedSize GetWidth(const AnchorPosResolutionParams& aParams) const {
    return AnchorResolvedSizeHelper::FromUnresolved(
        mWidth, mozilla::StylePhysicalAxis::Horizontal, aParams);
  }

  AnchorResolvedSize GetHeight(const AnchorPosResolutionParams& aParams) const {
    return AnchorResolvedSizeHelper::FromUnresolved(
        mHeight, mozilla::StylePhysicalAxis::Vertical, aParams);
  }

  AnchorResolvedSize GetMinWidth(
      const AnchorPosResolutionParams& aParams) const {
    return AnchorResolvedSizeHelper::FromUnresolved(
        mMinWidth, mozilla::StylePhysicalAxis::Horizontal, aParams);
  }

  AnchorResolvedSize GetMinHeight(
      const AnchorPosResolutionParams& aParams) const {
    return AnchorResolvedSizeHelper::FromUnresolved(
        mMinHeight, mozilla::StylePhysicalAxis::Vertical, aParams);
  }

  AnchorResolvedMaxSize GetMaxWidth(
      const AnchorPosResolutionParams& aParams) const {
    return AnchorResolvedMaxSizeHelper::FromUnresolved(
        mMaxWidth, mozilla::StylePhysicalAxis::Horizontal, aParams);
  }

  AnchorResolvedMaxSize GetMaxHeight(
      const AnchorPosResolutionParams& aParams) const {
    return AnchorResolvedMaxSizeHelper::FromUnresolved(
        mMaxHeight, mozilla::StylePhysicalAxis::Vertical, aParams);
  }

 private:
  template <typename SizeOrMaxSize>
  static bool ISizeCoordDependsOnContainer(const SizeOrMaxSize& aCoord) {
    if (aCoord.IsLengthPercentage()) {
      return aCoord.AsLengthPercentage().HasPercent();
    }
    return aCoord.IsFitContent() || aCoord.BehavesLikeStretchOnInlineAxis();
  }

  template <typename SizeOrMaxSize>
  static bool BSizeCoordDependsOnContainer(const SizeOrMaxSize& aCoord) {
    if (aCoord.IsLengthPercentage()) {
      return aCoord.AsLengthPercentage().HasPercent();
    }
    return aCoord.BehavesLikeStretchOnBlockAxis();
  }
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleTextReset {
  STYLE_STRUCT(nsStyleTextReset)
  nsStyleTextReset();

  // Note the difference between this and
  // ComputedStyle::HasTextDecorationLines.
  bool HasTextDecorationLines() const {
    return mTextDecorationLine != mozilla::StyleTextDecorationLine::NONE &&
           mTextDecorationLine !=
               mozilla::StyleTextDecorationLine::COLOR_OVERRIDE;
  }

  mozilla::StyleTextOverflow mTextOverflow;

  mozilla::StyleTextDecorationLine mTextDecorationLine;
  mozilla::StyleTextDecorationStyle mTextDecorationStyle;
  mozilla::StyleUnicodeBidi mUnicodeBidi;
  mozilla::StyleInitialLetter mInitialLetter;
  mozilla::StyleColor mTextDecorationColor;
  mozilla::StyleTextDecorationLength mTextDecorationThickness;
  mozilla::StyleTextDecorationInset mTextDecorationInset;
  mozilla::StyleTextBoxTrim mTextBoxTrim;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleText {
  STYLE_STRUCT(nsStyleText)
  explicit nsStyleText(const mozilla::dom::Document&);

  mozilla::StyleAbsoluteColor mColor;
  mozilla::StyleForcedColorAdjust mForcedColorAdjust;
  mozilla::StyleTextTransform mTextTransform;
  mozilla::StyleTextAlign mTextAlign;
  mozilla::StyleTextAlignLast mTextAlignLast;
  mozilla::StyleTextJustify mTextJustify;
  mozilla::StyleWhiteSpaceCollapse mWhiteSpaceCollapse =
      mozilla::StyleWhiteSpaceCollapse::Collapse;
  mozilla::StyleTextWrapMode mTextWrapMode = mozilla::StyleTextWrapMode::Wrap;
  mozilla::StyleLineBreak mLineBreak = mozilla::StyleLineBreak::Auto;

 private:
  mozilla::StyleWordBreak mWordBreak = mozilla::StyleWordBreak::Normal;
  mozilla::StyleOverflowWrap mOverflowWrap = mozilla::StyleOverflowWrap::Normal;
  mozilla::StyleTextAutospace mTextAutospace =
      mozilla::StyleTextAutospace::NO_AUTOSPACE;

 public:
  mozilla::StyleHyphens mHyphens;
  mozilla::StyleRubyAlign mRubyAlign;
  mozilla::StyleRubyPosition mRubyPosition;
  mozilla::StyleTextSizeAdjust mTextSizeAdjust;
  mozilla::StyleTextCombineUpright mTextCombineUpright;
  mozilla::StyleMozControlCharacterVisibility mMozControlCharacterVisibility;
  mozilla::StyleTextEmphasisPosition mTextEmphasisPosition;
  mozilla::StyleTextRendering mTextRendering;
  mozilla::StyleColor mTextEmphasisColor;
  mozilla::StyleColor mWebkitTextFillColor;
  mozilla::StyleColor mWebkitTextStrokeColor;

  mozilla::StyleNonNegativeLengthOrNumber mTabSize;
  mozilla::LengthPercentage mWordSpacing;
  mozilla::StyleLetterSpacing mLetterSpacing;
  mozilla::StyleTextIndent mTextIndent;

  mozilla::StyleTextBoxEdge mTextBoxEdge;

  mozilla::LengthPercentageOrAuto mTextUnderlineOffset;
  mozilla::StyleTextDecorationSkipInk mTextDecorationSkipInk;
  mozilla::StyleTextUnderlinePosition mTextUnderlinePosition;

  mozilla::StyleAu mWebkitTextStrokeWidth;

  mozilla::StyleArcSlice<mozilla::StyleSimpleShadow> mTextShadow;
  mozilla::StyleTextEmphasisStyle mTextEmphasisStyle;

  mozilla::StyleHyphenateCharacter mHyphenateCharacter =
      mozilla::StyleHyphenateCharacter::Auto();
  mozilla::StyleHyphenateLimitChars mHyphenateLimitChars =
      mozilla::StyleHyphenateLimitChars::Auto();

  mozilla::StyleTextSecurity mWebkitTextSecurity =
      mozilla::StyleTextSecurity::None;

  mozilla::StyleTextWrapStyle mTextWrapStyle =
      mozilla::StyleTextWrapStyle::Auto;

  char16_t TextSecurityMaskChar() const {
    switch (mWebkitTextSecurity) {
      case mozilla::StyleTextSecurity::None:
        return 0;
      case mozilla::StyleTextSecurity::Circle:
        return 0x25E6;
      case mozilla::StyleTextSecurity::Disc:
        return 0x2022;
      case mozilla::StyleTextSecurity::Square:
        return 0x25A0;
      default:
        MOZ_ASSERT_UNREACHABLE("unknown StyleTextSecurity value!");
        return 0;
    }
  }

  mozilla::StyleWordBreak EffectiveWordBreak() const {
    if (mWordBreak == mozilla::StyleWordBreak::BreakWord) {
      return mozilla::StyleWordBreak::Normal;
    }
    return mWordBreak;
  }

  mozilla::StyleOverflowWrap EffectiveOverflowWrap() const {
    if (mWordBreak == mozilla::StyleWordBreak::BreakWord) {
      return mozilla::StyleOverflowWrap::Anywhere;
    }
    return mOverflowWrap;
  }

  mozilla::StyleTextAutospace EffectiveTextAutospace() const {
    if (!mozilla::StaticPrefs::layout_css_text_autospace_enabled()) {
      return mozilla::StyleTextAutospace::NO_AUTOSPACE;
    }
    return mTextAutospace;
  }

  bool WhiteSpaceIsSignificant() const {
    return mWhiteSpaceCollapse != mozilla::StyleWhiteSpaceCollapse::Collapse &&
           mWhiteSpaceCollapse !=
               mozilla::StyleWhiteSpaceCollapse::PreserveBreaks;
  }

  bool WhiteSpaceCanHangOrVisuallyCollapse() const {
    // This was originally expressed in nsTextFrame in terms of:
    //   mWhiteSpace != StyleWhiteSpace::BreakSpaces &&
    //       WhiteSpaceCanWrapStyle() &&
    //       WhiteSpaceIsSignificant()
    // which simplifies to:
    return mTextWrapMode == mozilla::StyleTextWrapMode::Wrap &&
           mWhiteSpaceCollapse != mozilla::StyleWhiteSpaceCollapse::BreakSpaces;
  }

  bool NewlineIsSignificantStyle() const {
    return mWhiteSpaceCollapse == mozilla::StyleWhiteSpaceCollapse::Preserve ||
           mWhiteSpaceCollapse ==
               mozilla::StyleWhiteSpaceCollapse::PreserveBreaks ||
           mWhiteSpaceCollapse == mozilla::StyleWhiteSpaceCollapse::BreakSpaces;
  }

  bool WhiteSpaceOrNewlineIsSignificant() const {
    return NewlineIsSignificantStyle() || WhiteSpaceIsSignificant();
  }

  bool TabIsSignificant() const {
    return mWhiteSpaceCollapse == mozilla::StyleWhiteSpaceCollapse::Preserve ||
           mWhiteSpaceCollapse == mozilla::StyleWhiteSpaceCollapse::BreakSpaces;
  }

  bool WhiteSpaceCanWrapStyle() const {
    return mTextWrapMode == mozilla::StyleTextWrapMode::Wrap;
  }

  bool WordCanWrapStyle() const {
    if (!WhiteSpaceCanWrapStyle()) {
      return false;
    }
    auto owrap = EffectiveOverflowWrap();
    return owrap == mozilla::StyleOverflowWrap::BreakWord ||
           owrap == mozilla::StyleOverflowWrap::Anywhere;
  }

  bool HasEffectiveTextEmphasis() const {
    if (mTextEmphasisStyle.IsNone()) {
      return false;
    }
    if (mTextEmphasisStyle.IsString() &&
        mTextEmphasisStyle.AsString().AsString().IsEmpty()) {
      return false;
    }
    return true;
  }

  mozilla::StyleTextAlign TextAlignForLastLine() const {
    switch (mTextAlignLast) {
      case mozilla::StyleTextAlignLast::Auto:
        // 'text-align-last: auto' is equivalent to the value of the
        // 'text-align' property except when 'text-align' is set to 'justify',
        // in which case it is 'justify' when 'text-justify' is 'distribute' and
        // 'start' otherwise.
        //
        // XXX: the code below will have to change when we implement
        // text-justify
        if (mTextAlign == mozilla::StyleTextAlign::Justify) {
          return mozilla::StyleTextAlign::Start;
        }
        return mTextAlign;
      case mozilla::StyleTextAlignLast::Center:
        return mozilla::StyleTextAlign::Center;
      case mozilla::StyleTextAlignLast::Start:
        return mozilla::StyleTextAlign::Start;
      case mozilla::StyleTextAlignLast::End:
        return mozilla::StyleTextAlign::End;
      case mozilla::StyleTextAlignLast::Left:
        return mozilla::StyleTextAlign::Left;
      case mozilla::StyleTextAlignLast::Right:
        return mozilla::StyleTextAlign::Right;
      case mozilla::StyleTextAlignLast::Justify:
        return mozilla::StyleTextAlign::Justify;
    }
    return mozilla::StyleTextAlign::Start;
  }

  bool HasWebkitTextStroke() const { return mWebkitTextStrokeWidth > 0; }

  bool HasTextShadow() const { return !mTextShadow.IsEmpty(); }

  // The aContextFrame argument on each of these is the frame this
  // style struct is for.  If the frame is for SVG text or inside ruby,
  // the return value will be massaged to be something that makes sense
  // for those cases.
  inline bool NewlineIsSignificant(const nsTextFrame* aContextFrame) const;
  inline bool WhiteSpaceCanWrap(const nsIFrame* aContextFrame) const;
  inline bool WordCanWrap(const nsIFrame* aContextFrame) const;

  mozilla::LogicalSide TextEmphasisSide(mozilla::WritingMode aWM,
                                        const nsAtom* aLanguage) const;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleVisibility {
  STYLE_STRUCT(nsStyleVisibility)
  explicit nsStyleVisibility(const mozilla::dom::Document&);

  bool IsVisible() const {
    return mVisible == mozilla::StyleVisibility::Visible;
  }

  bool IsCollapse() const {
    return mVisible == mozilla::StyleVisibility::Collapse;
  }

  bool IsVisibleOrCollapsed() const {
    return mVisible == mozilla::StyleVisibility::Visible ||
           mVisible == mozilla::StyleVisibility::Collapse;
  }

  bool UseLegacyCollapseBehavior() const {
    return mMozBoxCollapse == mozilla::StyleBoxCollapse::Legacy;
  }

  /**
   * Given an image request, returns the orientation that should be used
   * on the image. The returned orientation may differ from the style
   * struct's orientation member value, if the image request is not of the
   * same origin.
   *
   * @param aRequest     The image request used to determine if same origin.
   */
  mozilla::StyleImageOrientation UsedImageOrientation(
      imgIRequest* aRequest) const {
    return UsedImageOrientation(aRequest, mImageOrientation);
  }

  /**
   * Given an image request and an orientation, returns the orientation
   * that should be used on the image. The returned orientation may differ
   * from the input orientation if the image request is not of the same
   * origin.
   *
   * @param aRequest     The image request used to determine if same origin.
   * @param aOrientation The input orientation.
   */
  static mozilla::StyleImageOrientation UsedImageOrientation(
      imgIRequest* aRequest, mozilla::StyleImageOrientation aOrientation);

  mozilla::StyleDirection mDirection;
  mozilla::StyleVisibility mVisible;
  mozilla::StyleImageRendering mImageRendering;

 private:
  mozilla::StyleImageOrientation mImageOrientation;

 public:
  mozilla::StyleImageDecoding mImageDecoding;
  mozilla::StyleWritingModeProperty mWritingMode;
  mozilla::StyleTextOrientation mTextOrientation;
  mozilla::StyleBoxCollapse mMozBoxCollapse;
  mozilla::StylePrintColorAdjust mPrintColorAdjust;
  mozilla::StyleDominantBaseline mDominantBaseline;
};

namespace mozilla {

// Note that IsAuto() does not exclude the possibility that `left` or `right`
// is set; it refers only to behavior in horizontal typographic mode.
inline bool StyleTextUnderlinePosition::IsAuto() const {
  return !(*this & (StyleTextUnderlinePosition::FROM_FONT |
                    StyleTextUnderlinePosition::UNDER));
}
inline bool StyleTextUnderlinePosition::IsFromFont() const {
  return bool(*this & StyleTextUnderlinePosition::FROM_FONT);
}
inline bool StyleTextUnderlinePosition::IsUnder() const {
  return bool(*this & StyleTextUnderlinePosition::UNDER);
}
inline bool StyleTextUnderlinePosition::IsLeft() const {
  return bool(*this & StyleTextUnderlinePosition::LEFT);
}
inline bool StyleTextUnderlinePosition::IsRight() const {
  return bool(*this & StyleTextUnderlinePosition::RIGHT);
}

struct StyleTransition {
  StyleTransition() = default;
  explicit StyleTransition(const StyleTransition& aCopy);
  const StyleComputedTimingFunction& GetTimingFunction() const {
    return mTimingFunction;
  }
  const StyleTime& GetDelay() const { return mDelay; }
  const StyleTime& GetDuration() const { return mDuration; }
  const StyleTransitionProperty& GetProperty() const { return mProperty; }
  StyleTransitionBehavior GetBehavior() const { return mBehavior; }

  bool operator==(const StyleTransition&) const = default;

 private:
  StyleComputedTimingFunction mTimingFunction{
      StyleComputedTimingFunction::Keyword(StyleTimingKeyword::Ease)};
  StyleTime mDuration{0.0};
  StyleTime mDelay{0.0};
  StyleTransitionProperty mProperty{StyleTransitionProperty::NonCustom(
      StyleNonCustomPropertyId{uint16_t(eCSSProperty_all)})};
  StyleTransitionBehavior mBehavior = StyleTransitionBehavior::Normal;
};

struct StyleAnimation {
  StyleAnimation() = default;
  explicit StyleAnimation(const StyleAnimation& aCopy);

  const StyleComputedTimingFunction& GetTimingFunction() const {
    return mTimingFunction;
  }
  const StyleTime& GetDelay() const { return mDelay; }
  const StyleAnimationDuration& GetDuration() const { return mDuration; }
  nsAtom* GetName() const { return mName._0.AsAtom(); }
  StyleAnimationDirection GetDirection() const { return mDirection; }
  StyleAnimationFillMode GetFillMode() const { return mFillMode; }
  StyleAnimationPlayState GetPlayState() const { return mPlayState; }
  float GetIterationCount() const { return mIterationCount._0; }
  StyleAnimationComposition GetComposition() const { return mComposition; }
  const StyleAnimationTimeline& GetTimeline() const { return mTimeline; }
  const StyleAnimationRangeStart& GetRangeStart() const { return mRangeStart; }
  const StyleAnimationRangeEnd& GetRangeEnd() const { return mRangeEnd; }

  bool operator==(const StyleAnimation&) const = default;

 private:
  StyleComputedTimingFunction mTimingFunction{
      StyleComputedTimingFunction::Keyword(StyleTimingKeyword::Ease)};
  StyleAnimationDuration mDuration = StyleAnimationDuration::Auto();
  StyleTime mDelay{0.0f};
  StyleAnimationName mName;
  StyleAnimationDirection mDirection = StyleAnimationDirection::Normal;
  StyleAnimationFillMode mFillMode = StyleAnimationFillMode::None;
  StyleAnimationPlayState mPlayState = StyleAnimationPlayState::Running;
  StyleAnimationIterationCount mIterationCount{1.0f};
  StyleAnimationComposition mComposition = StyleAnimationComposition::Replace;
  StyleAnimationTimeline mTimeline = StyleAnimationTimeline::Auto();
  StyleAnimationRangeStart mRangeStart =
      StyleAnimationRangeStart::DefaultStart();
  StyleAnimationRangeEnd mRangeEnd = StyleAnimationRangeEnd::DefaultEnd();
};

struct StyleScrollTimeline {
  StyleScrollTimeline() = default;
  explicit StyleScrollTimeline(const StyleScrollTimeline& aCopy) = default;

  const StyleTimelineName& GetName() const { return mName; }
  StyleScrollAxis GetAxis() const { return mAxis; }

  bool operator==(const StyleScrollTimeline&) const = default;

 private:
  StyleTimelineName mName;
  StyleScrollAxis mAxis = StyleScrollAxis::Block;
};

struct StyleViewTimeline {
  StyleViewTimeline() = default;
  explicit StyleViewTimeline(const StyleViewTimeline& aCopy) = default;

  const StyleTimelineName& GetName() const { return mName; }
  StyleScrollAxis GetAxis() const { return mAxis; }
  const StyleViewTimelineInset& GetInset() const { return mInset; }

  bool operator==(const StyleViewTimeline&) const = default;

 private:
  StyleTimelineName mName;
  StyleScrollAxis mAxis = StyleScrollAxis::Block;
  StyleViewTimelineInset mInset;
};

}  // namespace mozilla

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleDisplay {
  STYLE_STRUCT(nsStyleDisplay)
  nsStyleDisplay();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleDisplay*);

  using StyleContain = mozilla::StyleContain;
  using StyleContentVisibility = mozilla::StyleContentVisibility;

  nsChangeHint CalcDifference(const nsStyleDisplay& aNewData,
                              const mozilla::ComputedStyle& aOldStyle) const;

  nsChangeHint CalcTransformPropertyDifference(
      const nsStyleDisplay& aNewData) const;

  mozilla::StyleDisplay mDisplay;
  // Saved mDisplay for position:absolute/fixed and float:left/right; otherwise
  // equal to mDisplay.
  mozilla::StyleDisplay mOriginalDisplay;
  // Equal to mContain plus any implicit containment from mContentVisibility and
  // mContainerType.
  mozilla::StyleContentVisibility mContentVisibility;
  mozilla::StyleContainerType mContainerType;

  bool IsQueryContainer() const {
    return mContainerType != mozilla::StyleContainerType::NORMAL;
  }

  // See WritingModes.h for the implementations.
  inline mozilla::UsedFloat UsedFloat(mozilla::WritingMode aWM) const;
  inline mozilla::UsedClear UsedClear(mozilla::WritingMode aWM) const;

 private:
  mozilla::StyleAppearance mAppearance;
  mozilla::StyleContain mContain;
  // Equal to mContain plus any implicit containment from mContentVisibility and
  // mContainerType.
  mozilla::StyleContain mEffectiveContainment;

 public:
  mozilla::StyleAppearance mDefaultAppearance;
  mozilla::StylePositionProperty mPosition;

  mozilla::StyleFloat mFloat;
  mozilla::StyleClear mClear;
  mozilla::StyleBreakWithin mBreakInside;
  mozilla::StyleBreakBetween mBreakBefore;
  mozilla::StyleBreakBetween mBreakAfter;
  mozilla::StyleOverflow mOverflowX;
  mozilla::StyleOverflow mOverflowY;
  mozilla::StyleScrollbarGutter mScrollbarGutter;
  mozilla::StyleResize mResize;
  mozilla::StyleOrient mOrient;
  mozilla::StyleIsolation mIsolation;
  mozilla::StyleTopLayer mTopLayer;

  mozilla::StyleTouchAction mTouchAction;
  mozilla::StyleScrollBehavior mScrollBehavior;
  mozilla::StyleOverscrollBehavior mOverscrollBehaviorX;
  mozilla::StyleOverscrollBehavior mOverscrollBehaviorY;
  mozilla::StyleOverflowAnchor mOverflowAnchor;
  mozilla::StyleScrollSnapAlign mScrollSnapAlign;
  mozilla::StyleScrollSnapStop mScrollSnapStop;
  mozilla::StyleScrollSnapType mScrollSnapType;

  mozilla::StyleBackfaceVisibility mBackfaceVisibility;
  mozilla::StyleTransformStyle mTransformStyle;
  mozilla::StyleTransformBox mTransformBox;

  mozilla::StyleTransform mTransform;
  mozilla::StyleRotate mRotate;

  mozilla::StyleTranslate mTranslate;
  mozilla::StyleScale mScale;

  mozilla::StyleContainerName mContainerName;
  mozilla::StyleWillChange mWillChange;

  mozilla::StyleOffsetPath mOffsetPath;
  mozilla::LengthPercentage mOffsetDistance;
  mozilla::StyleOffsetRotate mOffsetRotate;
  mozilla::StylePositionOrAuto mOffsetAnchor;
  mozilla::StyleOffsetPosition mOffsetPosition;

  mozilla::StyleTransformOrigin mTransformOrigin;
  mozilla::StylePerspective mChildPerspective;
  mozilla::Position mPerspectiveOrigin;

  mozilla::StyleAlignmentBaseline mAlignmentBaseline;
  mozilla::StyleBaselineShift mBaselineShift;
  mozilla::StyleBaselineSource mBaselineSource;

  mozilla::StyleLineClamp mWebkitLineClamp;

  // The threshold used for extracting a shape from shape-outside: <image>.
  float mShapeImageThreshold = 0.0f;

  mozilla::StyleZoom mZoom = mozilla::StyleZoom::ONE;

  // The margin around a shape-outside: <image>.
  mozilla::NonNegativeLengthPercentage mShapeMargin;

  mozilla::StyleShapeOutside mShapeOutside;

  // 'none', 'all', or a list of one or more `<dashed-ident>` identifiers that
  // anchor positioned elements may reference.
  mozilla::StyleAnchorName mAnchorName;

  // 'none', 'all', or a list of one or more `<dashed-ident>` identifiers that
  // may identify anchor positioning anchor elements.
  mozilla::StyleScopedName mAnchorScope;

  mozilla::Maybe<mozilla::WindowButtonType> GetWindowButtonType() const {
    if (MOZ_LIKELY(mDefaultAppearance == mozilla::StyleAppearance::None)) {
      return mozilla::Nothing();
    }
    switch (mDefaultAppearance) {
      case mozilla::StyleAppearance::MozWindowButtonMaximize:
      case mozilla::StyleAppearance::MozWindowButtonRestore:
        return Some(mozilla::WindowButtonType::Maximize);
      case mozilla::StyleAppearance::MozWindowButtonMinimize:
        return Some(mozilla::WindowButtonType::Minimize);
      case mozilla::StyleAppearance::MozWindowButtonClose:
        return Some(mozilla::WindowButtonType::Close);
      default:
        return mozilla::Nothing();
    }
  }

  bool HasNativeAppearance() const {
    auto appearance = EffectiveAppearance();
    return appearance != mozilla::StyleAppearance::None &&
           appearance != mozilla::StyleAppearance::Base &&
           appearance != mozilla::StyleAppearance::BaseSelect;
  }

  mozilla::StyleAppearance EffectiveAppearance() const {
    if (MOZ_LIKELY(mAppearance == mozilla::StyleAppearance::None)) {
      return mAppearance;
    }
    switch (mAppearance) {
      case mozilla::StyleAppearance::Auto:
      case mozilla::StyleAppearance::Button:
      case mozilla::StyleAppearance::Searchfield:
      case mozilla::StyleAppearance::Textarea:
      case mozilla::StyleAppearance::Checkbox:
      case mozilla::StyleAppearance::Radio:
      case mozilla::StyleAppearance::Menulist:
      case mozilla::StyleAppearance::Listbox:
      case mozilla::StyleAppearance::Meter:
      case mozilla::StyleAppearance::ProgressBar:
        // These are all the values that behave like `auto`.
        return mDefaultAppearance;
      case mozilla::StyleAppearance::Textfield:
        // `appearance: textfield` should behave like `auto` on all elements
        // except <input type=search/number/password> elements, which we
        // identify using the internal -moz-default-appearance property.
        if (mDefaultAppearance == mozilla::StyleAppearance::NumberInput ||
            mDefaultAppearance == mozilla::StyleAppearance::PasswordInput) {
          return mAppearance;
        }
        return mDefaultAppearance;
      case mozilla::StyleAppearance::MenulistButton:
        // `appearance: menulist-button` should behave like `auto` on all
        // elements except for drop down selects.
        if (mDefaultAppearance == mozilla::StyleAppearance::Menulist) {
          return mAppearance;
        }
        return mDefaultAppearance;
      default:
        return mAppearance;
    }
  }

  mozilla::StyleDisplayOutside DisplayOutside() const {
    return mDisplay.Outside();
  }
  mozilla::StyleDisplayInside DisplayInside() const {
    return mDisplay.Inside();
  }
  bool IsListItem() const { return mDisplay.IsListItem(); }
  bool IsInlineFlow() const { return mDisplay.IsInlineFlow(); }

  bool IsInlineInsideStyle() const { return mDisplay.IsInlineInside(); }

  bool IsBlockOutsideStyle() const {
    return DisplayOutside() == mozilla::StyleDisplayOutside::Block;
  }

  bool IsInlineOutsideStyle() const { return mDisplay.IsInlineOutside(); }

  bool IsOriginalDisplayInlineOutside() const {
    return mOriginalDisplay.IsInlineOutside();
  }

  bool IsInnerTableStyle() const { return mDisplay.IsInternalTable(); }

  bool IsInternalTableStyleExceptCell() const {
    return mDisplay.IsInternalTableExceptCell();
  }

  bool IsFloatingStyle() const { return mozilla::StyleFloat::None != mFloat; }

  bool IsPositionedStyle() const {
    return mPosition != mozilla::StylePositionProperty::Static ||
           (mWillChange.bits & mozilla::StyleWillChangeBits::POSITION);
  }

  bool IsAbsolutelyPositionedStyle() const {
    return mozilla::StylePositionProperty::Absolute == mPosition ||
           mozilla::StylePositionProperty::Fixed == mPosition;
  }

  bool IsRelativelyOrStickyPositionedStyle() const {
    return mozilla::StylePositionProperty::Relative == mPosition ||
           mozilla::StylePositionProperty::Sticky == mPosition;
  }
  bool IsRelativelyPositionedStyle() const {
    return mozilla::StylePositionProperty::Relative == mPosition;
  }
  bool IsStickyPositionedStyle() const {
    return mozilla::StylePositionProperty::Sticky == mPosition;
  }
  bool IsPositionForcingStackingContext() const {
    return mozilla::StylePositionProperty::Sticky == mPosition ||
           mozilla::StylePositionProperty::Fixed == mPosition;
  }

  bool HasAnchorName() const { return !mAnchorName.IsEmpty(); }

  bool IsRubyDisplayType() const { return mDisplay.IsRuby(); }

  bool IsInternalRubyDisplayType() const { return mDisplay.IsInternalRuby(); }

  bool IsOutOfFlowStyle() const {
    return (IsAbsolutelyPositionedStyle() || IsFloatingStyle());
  }

  bool IsScrollableOverflow() const {
    // Visible and Clip can be combined but not with other values,
    // so checking mOverflowX is enough.
    return mOverflowX != mozilla::StyleOverflow::Visible &&
           mOverflowX != mozilla::StyleOverflow::Clip;
  }

  bool OverflowIsVisibleInBothAxis() const {
    return mOverflowX == mozilla::StyleOverflow::Visible &&
           mOverflowY == mozilla::StyleOverflow::Visible;
  }

  bool IsContainPaint() const {
    // Short circuit for no containment whatsoever
    if (!mEffectiveContainment) {
      return false;
    }
    return (mEffectiveContainment & StyleContain::PAINT) &&
           !IsInternalRubyDisplayType() && !IsInternalTableStyleExceptCell();
  }

  bool IsContainLayout() const {
    // Short circuit for no containment whatsoever
    if (!mEffectiveContainment) {
      return false;
    }
    // Note: The spec for layout containment says it should
    // have no effect on non-atomic, inline-level boxes. We
    // don't check for these here because we don't know
    // what type of element is involved. Callers are
    // responsible for checking if the box in question is
    // non-atomic and inline-level, and creating an
    // exemption as necessary.
    return (mEffectiveContainment & StyleContain::LAYOUT) &&
           !IsInternalRubyDisplayType() && !IsInternalTableStyleExceptCell();
  }

  bool IsContainStyle() const {
    return !!(mEffectiveContainment & StyleContain::STYLE);
  }

  bool IsContainAny() const { return !!mEffectiveContainment; }

  // This is similar to PrecludesSizeContainmentOrContentVisibility, but also
  // takes into account whether or not the given frame is a non-atomic,
  // inline-level box.
  bool PrecludesSizeContainmentOrContentVisibilityWithFrame(
      const nsIFrame&) const;

  StyleContentVisibility ContentVisibility(const nsIFrame&) const;

  /* Returns whether the element has the transform property or a related
   * property. */
  bool HasTransformStyle() const {
    return HasTransformProperty() || HasIndividualTransform() ||
           mTransformStyle == mozilla::StyleTransformStyle::Preserve3d ||
           (mWillChange.bits & mozilla::StyleWillChangeBits::TRANSFORM) ||
           !mOffsetPath.IsNone();
  }

  bool HasTransformProperty() const { return !mTransform._0.IsEmpty(); }

  bool HasIndividualTransform() const {
    return !mRotate.IsNone() || !mTranslate.IsNone() || !mScale.IsNone();
  }

  bool HasPerspectiveStyle() const { return !mChildPerspective.IsNone(); }

  bool BackfaceIsHidden() const {
    return mBackfaceVisibility == mozilla::StyleBackfaceVisibility::Hidden;
  }

  // FIXME(emilio): This should be more fine-grained on each caller to
  // BreakBefore() / BreakAfter().
  static bool ShouldBreak(mozilla::StyleBreakBetween aBreak) {
    switch (aBreak) {
      case mozilla::StyleBreakBetween::Left:
      case mozilla::StyleBreakBetween::Right:
      case mozilla::StyleBreakBetween::Page:
      case mozilla::StyleBreakBetween::Always:
        return true;
      case mozilla::StyleBreakBetween::Auto:
      case mozilla::StyleBreakBetween::Avoid:
        return false;
      default:
        MOZ_ASSERT_UNREACHABLE("Unknown break kind");
        return false;
    }
  }

  // These two methods are deprecated since they do not differentiate paginated
  // context and multi-column context. Use nsIFrame::ShouldBreakBefore() /
  // nsIFrame::ShouldBreakAfter() instead.
  bool BreakBefore() const { return ShouldBreak(mBreakBefore); }
  bool BreakAfter() const { return ShouldBreak(mBreakAfter); }

  // These are defined in nsStyleStructInlines.h.

  // The aContextFrame argument on each of these is the frame this
  // style struct is for.  If the frame is for SVG text, the return
  // value will be massaged to be something that makes sense for
  // SVG text.
  inline bool IsBlockOutside(const nsIFrame* aContextFrame) const;
  inline bool IsInlineOutside(const nsIFrame* aContextFrame) const;
  inline mozilla::StyleDisplay GetDisplay(const nsIFrame* aContextFrame) const;
  inline bool IsFloating(const nsIFrame* aContextFrame) const;
  inline bool IsRelativelyOrStickyPositioned(
      const nsIFrame* aContextFrame) const;

  // Note: In general, you'd want to call IsRelativelyOrStickyPositioned()
  // unless you want to deal with "position:relative" and "position:sticky"
  // differently.
  inline bool IsRelativelyPositioned(const nsIFrame* aContextFrame) const;
  inline bool IsStickyPositioned(const nsIFrame* aContextFrame) const;

  inline bool IsAbsolutelyPositioned(const nsIFrame* aContextFrame) const;

  // These methods are defined in nsStyleStructInlines.h.

  /**
   * Returns true when the element has the transform property
   * or a related property, and supports CSS transforms.
   * aContextFrame is the frame for which this is the nsStyleDisplay.
   */
  inline bool HasTransform(const nsIFrame* aContextFrame) const;

  /**
   * Returns true when the element has the perspective property,
   * and supports CSS transforms. aContextFrame is the frame for
   * which this is the nsStyleDisplay.
   */
  inline bool HasPerspective(const nsIFrame* aContextFrame) const;

  inline bool
  IsFixedPosContainingBlockForContainLayoutAndPaintSupportingFrames() const;
  inline bool IsFixedPosContainingBlockForTransformSupportingFrames() const;

  mozilla::ContainSizeAxes GetContainSizeAxes(const nsIFrame& aFrame) const;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleTable {
  STYLE_STRUCT(nsStyleTable)
  nsStyleTable();

  mozilla::StyleTableLayout mLayoutStrategy;
  int32_t mXSpan;  // The number of columns spanned by a colgroup or col
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleTableBorder {
  STYLE_STRUCT(nsStyleTableBorder)
  nsStyleTableBorder();

  mozilla::StyleBorderSpacing mBorderSpacing;
  mozilla::StyleBorderCollapse mBorderCollapse;
  mozilla::StyleCaptionSide mCaptionSide;
  mozilla::StyleEmptyCells mEmptyCells;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleContent {
  STYLE_STRUCT(nsStyleContent)
  nsStyleContent();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleContent*);

  using CounterPair = mozilla::StyleGenericCounterPair<int32_t>;

  /// Returns the content items that aren't alternative content.
  mozilla::Span<const mozilla::StyleContentItem> NonAltContentItems() const {
    if (!mContent.IsItems()) {
      return {};
    }
    const auto& items = mContent.AsItems();
    return mozilla::Span(items.items).To(items.alt_start);
  }

  /// Returns the content items that /are/ alternative content.
  mozilla::Span<const mozilla::StyleContentItem> AltContentItems() const {
    if (!mContent.IsItems()) {
      return {};
    }
    const auto& items = mContent.AsItems();
    return mozilla::Span(items.items).From(items.alt_start);
  }

  /**
   * True if the 'content' change from aOld to aNew can be applied by rewriting
   * the existing generated text nodes in place rather than reframing the full
   * subtree.
   */
  static bool CanUpdateGeneratedContentText(const nsStyleContent& aOld,
                                            const nsStyleContent& aNew);

  mozilla::StyleContent mContent;
  mozilla::StyleCounterIncrement mCounterIncrement;
  mozilla::StyleCounterReset mCounterReset;
  mozilla::StyleCounterSet mCounterSet;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleUIReset {
  STYLE_STRUCT(nsStyleUIReset)
  nsStyleUIReset();

 private:
  mozilla::StyleUserSelect mUserSelect;  // Use ComputedStyle::UserSelect()
  mozilla::StyleScrollbarWidth mScrollbarWidth;  // Use ScrollbarWidth()

 public:
  mozilla::StyleUserSelect ComputedUserSelect() const { return mUserSelect; }

  // DO NOT USE THIS FUNCTION DIRECTLY.
  // nsLayoutUtils::ScrollbarWidthFor() should be used instead.
  mozilla::StyleScrollbarWidth ComputedScrollbarWidth() const {
    return mScrollbarWidth;
  }

  const mozilla::StyleTransitionProperty& GetTransitionProperty(
      uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionPropertyCount].GetProperty();
  }
  const mozilla::StyleTime& GetTransitionDelay(uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionDelayCount].GetDelay();
  }
  const mozilla::StyleTime& GetTransitionDuration(uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionDurationCount].GetDuration();
  }
  const mozilla::StyleComputedTimingFunction& GetTransitionTimingFunction(
      uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionTimingFunctionCount]
        .GetTimingFunction();
  }
  mozilla::StyleTransitionBehavior GetTransitionBehavior(
      uint32_t aIndex) const {
    return mTransitions[aIndex % mTransitionBehaviorCount].GetBehavior();
  }
  mozilla::StyleTime GetTransitionCombinedDuration(uint32_t aIndex) const {
    // https://drafts.csswg.org/css-transitions/#transition-combined-duration
    return {std::max(GetTransitionDuration(aIndex).seconds, 0.0f) +
            GetTransitionDelay(aIndex).seconds};
  }

  nsAtom* GetAnimationName(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationNameCount].GetName();
  }
  const mozilla::StyleTime& GetAnimationDelay(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationDelayCount].GetDelay();
  }
  const mozilla::StyleAnimationDuration& GetAnimationDuration(
      uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationDurationCount].GetDuration();
  }
  mozilla::StyleAnimationDirection GetAnimationDirection(
      uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationDirectionCount].GetDirection();
  }
  mozilla::StyleAnimationFillMode GetAnimationFillMode(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationFillModeCount].GetFillMode();
  }
  mozilla::StyleAnimationPlayState GetAnimationPlayState(
      uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationPlayStateCount].GetPlayState();
  }
  float GetAnimationIterationCount(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationIterationCountCount]
        .GetIterationCount();
  }
  const mozilla::StyleComputedTimingFunction& GetAnimationTimingFunction(
      uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationTimingFunctionCount]
        .GetTimingFunction();
  }
  mozilla::StyleAnimationComposition GetAnimationComposition(
      uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationCompositionCount].GetComposition();
  }
  const mozilla::StyleAnimationTimeline& GetTimeline(uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationTimelineCount].GetTimeline();
  }
  const mozilla::StyleAnimationRangeStart& GetAnimationRangeStart(
      uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationRangeStartCount].GetRangeStart();
  }
  const mozilla::StyleAnimationRangeEnd& GetAnimationRangeEnd(
      uint32_t aIndex) const {
    return mAnimations[aIndex % mAnimationRangeEndCount].GetRangeEnd();
  }

  const mozilla::StyleTimelineName& GetScrollTimelineName(
      uint32_t aIndex) const {
    return mScrollTimelines[aIndex % mScrollTimelineNameCount].GetName();
  }
  mozilla::StyleScrollAxis GetScrollTimelineAxis(uint32_t aIndex) const {
    return mScrollTimelines[aIndex % mScrollTimelineAxisCount].GetAxis();
  }

  const mozilla::StyleTimelineName& GetViewTimelineName(uint32_t aIndex) const {
    return mViewTimelines[aIndex % mViewTimelineNameCount].GetName();
  }
  mozilla::StyleScrollAxis GetViewTimelineAxis(uint32_t aIndex) const {
    return mViewTimelines[aIndex % mViewTimelineAxisCount].GetAxis();
  }
  const mozilla::StyleViewTimelineInset& GetViewTimelineInset(
      uint32_t aIndex) const {
    return mViewTimelines[aIndex % mViewTimelineInsetCount].GetInset();
  }

  mozilla::StyleBoolInteger mMozForceBrokenImageIcon;
  mozilla::StyleBoolInteger mMozSubtreeHiddenOnlyVisually;
  mozilla::StyleImeMode mIMEMode;
  mozilla::StyleWindowDragging mWindowDragging;
  mozilla::StyleWindowShadow mWindowShadow;
  mozilla::StyleFieldSizing mFieldSizing;

  // How far one line scrolls this scroll container, when it doesn't want the
  // font-derived amount. See ScrollContainerFrame::GetLineScrollAmount().
  mozilla::NonNegativeLengthOrAuto mMozLineScrollAmount;

  // The margin of the window region that should be transparent to events.
  mozilla::StyleLength mMozWindowInputRegionMargin;
  mozilla::StyleTransform mMozWindowTransform;

  nsStyleAutoArray<mozilla::StyleTransition> mTransitions;
  // The number of elements in mTransitions that are not from repeating
  // a list due to another property being longer.
  uint32_t mTransitionTimingFunctionCount;
  uint32_t mTransitionDurationCount;
  uint32_t mTransitionDelayCount;
  uint32_t mTransitionPropertyCount;
  uint32_t mTransitionBehaviorCount;
  float mWindowOpacity;
  nsStyleAutoArray<mozilla::StyleAnimation> mAnimations;
  // The number of elements in mAnimations that are not from repeating
  // a list due to another property being longer.
  uint32_t mAnimationTimingFunctionCount;
  uint32_t mAnimationDurationCount;
  uint32_t mAnimationDelayCount;
  uint32_t mAnimationNameCount;
  uint32_t mAnimationDirectionCount;
  uint32_t mAnimationFillModeCount;
  uint32_t mAnimationPlayStateCount;
  uint32_t mAnimationIterationCountCount;
  uint32_t mAnimationCompositionCount;
  uint32_t mAnimationTimelineCount;
  uint32_t mAnimationRangeStartCount;
  uint32_t mAnimationRangeEndCount;

  nsStyleAutoArray<mozilla::StyleScrollTimeline> mScrollTimelines;
  uint32_t mScrollTimelineNameCount;
  uint32_t mScrollTimelineAxisCount;

  nsStyleAutoArray<mozilla::StyleViewTimeline> mViewTimelines;
  uint32_t mViewTimelineNameCount;
  uint32_t mViewTimelineAxisCount;
  uint32_t mViewTimelineInsetCount;

  bool HasViewTransitionName() const {
    return mViewTransitionName.value.AsAtom() != nsGkAtoms::none;
  }

  mozilla::StyleViewTransitionName mViewTransitionName;
  mozilla::StyleViewTransitionClass mViewTransitionClass;

  mozilla::StyleScopedName mTimelineScope;

  mozilla::StyleLinkParameters mLinkParameters;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleUI {
  STYLE_STRUCT(nsStyleUI)
  nsStyleUI();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleUI*);

  mozilla::StyleInert mInert;
  mozilla::StyleMozTheme mMozTheme;

 private:
  mozilla::StyleUserFocus mUserFocus;
  mozilla::StylePointerEvents mPointerEvents;
  mozilla::StyleCursor mCursor;

 public:
  bool IsInert() const { return mInert == mozilla::StyleInert::Inert; }

  mozilla::StyleUserFocus UserFocus() const {
    return IsInert() ? mozilla::StyleUserFocus::None : mUserFocus;
  }

  // This is likely not the getter you want (you probably want
  // ComputedStyle::PointerEvents().
  mozilla::StylePointerEvents ComputedPointerEvents() const {
    return mPointerEvents;
  }

  const mozilla::StyleCursor& Cursor() const {
    static mozilla::StyleCursor sAuto{{}, mozilla::StyleCursorKind::Auto};
    return IsInert() ? sAuto : mCursor;
  }

  mozilla::StyleColorOrAuto mAccentColor;
  mozilla::StyleCaretColor mCaretColor;
  mozilla::StyleScrollbarColor mScrollbarColor;
  mozilla::StyleColorScheme mColorScheme;

  bool HasCustomScrollbars() const { return !mScrollbarColor.IsAuto(); }
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleXUL {
  STYLE_STRUCT(nsStyleXUL)
  nsStyleXUL();

  float mBoxFlex;
  int32_t mBoxOrdinal;
  mozilla::StyleBoxAlign mBoxAlign;
  mozilla::StyleBoxDirection mBoxDirection;
  mozilla::StyleBoxOrient mBoxOrient;
  mozilla::StyleBoxPack mBoxPack;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleColumn {
  STYLE_STRUCT(nsStyleColumn)
  nsStyleColumn();

  mozilla::StyleColumnCount mColumnCount = mozilla::StyleColumnCount::Auto();
  mozilla::NonNegativeLengthOrAuto mColumnWidth;

  mozilla::StyleColor mColumnRuleColor;
  mozilla::StyleBorderStyle mColumnRuleStyle;
  mozilla::StyleColumnFill mColumnFill = mozilla::StyleColumnFill::Balance;
  mozilla::StyleColumnSpan mColumnSpan = mozilla::StyleColumnSpan::None;

  nscoord GetColumnRuleWidth() const {
    if (!IsVisibleBorderStyle(mColumnRuleStyle)) {
      return 0;
    }
    return mColumnRuleWidth;
  }

  bool IsColumnContainerStyle() const {
    return !mColumnCount.IsAuto() || !mColumnWidth.IsAuto();
  }

  bool IsColumnSpanStyle() const {
    return mColumnSpan == mozilla::StyleColumnSpan::All;
  }

 protected:
  mozilla::StyleBorderSideWidth mColumnRuleWidth;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleSVG {
  STYLE_STRUCT(nsStyleSVG)
  nsStyleSVG();

  mozilla::StyleSVGPaint mFill;
  mozilla::StyleSVGPaint mStroke;
  mozilla::StyleUrlOrNone mMarkerEnd;
  mozilla::StyleUrlOrNone mMarkerMid;
  mozilla::StyleUrlOrNone mMarkerStart;
  mozilla::StyleMozContextProperties mMozContextProperties;

  mozilla::StyleSVGStrokeDashArray mStrokeDasharray;
  mozilla::StyleSVGLength mStrokeDashoffset;
  mozilla::StyleSVGWidth mStrokeWidth;

  mozilla::StyleSVGOpacity mFillOpacity;
  float mStrokeMiterlimit;
  mozilla::StyleSVGOpacity mStrokeOpacity;

  mozilla::StyleFillRule mClipRule;
  mozilla::StyleColorInterpolation mColorInterpolation;
  mozilla::StyleColorInterpolation mColorInterpolationFilters;
  mozilla::StyleFillRule mFillRule;
  mozilla::StyleSVGPaintOrder mPaintOrder;
  mozilla::StyleShapeRendering mShapeRendering;
  mozilla::StyleStrokeLinecap mStrokeLinecap;
  mozilla::StyleStrokeLinejoin mStrokeLinejoin;
  mozilla::StyleTextAnchor mTextAnchor;

  /// Returns true if style has been set to expose the computed values of
  /// certain properties (such as 'fill') to the contents of any linked images.
  bool ExposesContextProperties() const {
    return bool(mMozContextProperties.bits);
  }

  bool HasMarker() const {
    return mMarkerStart.IsUrl() || mMarkerMid.IsUrl() || mMarkerEnd.IsUrl();
  }

  /**
   * Returns true if the stroke is not "none" and the stroke-opacity is greater
   * than zero (or a context-dependent value).
   *
   * This ignores stroke-widths as that depends on the context.
   */
  bool HasStroke() const {
    if (mStroke.kind.IsNone()) {
      return false;
    }
    return !mStrokeOpacity.IsOpacity() || mStrokeOpacity.AsOpacity() > 0;
  }

  /**
   * Returns true if the fill is not "none" and the fill-opacity is greater
   * than zero (or a context-dependent value).
   */
  bool HasFill() const {
    if (mFill.kind.IsNone()) {
      return false;
    }
    return !mFillOpacity.IsOpacity() || mFillOpacity.AsOpacity() > 0;
  }
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleSVGReset {
  STYLE_STRUCT(nsStyleSVGReset)
  nsStyleSVGReset();
  void TriggerImageLoads(mozilla::dom::Document&, const nsStyleSVGReset*);

  bool HasClipPath() const { return !mClipPath.IsNone(); }

  bool HasMask() const;

  bool HasNonScalingStroke() const {
    return mVectorEffect.HasNonScalingStroke();
  }

  // geometry properties
  mozilla::LengthPercentage mX;
  mozilla::LengthPercentage mY;
  mozilla::LengthPercentage mCx;
  mozilla::LengthPercentage mCy;
  mozilla::NonNegativeLengthPercentageOrAuto mRx;
  mozilla::NonNegativeLengthPercentageOrAuto mRy;
  mozilla::NonNegativeLengthPercentage mR;

  nsStyleImageLayers mMask;
  mozilla::StyleClipPath mClipPath;
  mozilla::StyleColor mStopColor;
  mozilla::StyleColor mFloodColor;
  mozilla::StyleColor mLightingColor;

  float mStopOpacity;
  float mFloodOpacity;

  mozilla::StyleVectorEffect mVectorEffect;
  mozilla::StyleMaskType mMaskType;

  mozilla::StyleDProperty mD;
};

struct MOZ_NEEDS_MEMMOVABLE_MEMBERS nsStyleEffects {
  STYLE_STRUCT(nsStyleEffects)
  nsStyleEffects();

  bool HasFilters() const { return !mFilters.IsEmpty(); }

  bool HasBackdropFilters() const { return !mBackdropFilters.IsEmpty(); }

  bool HasBoxShadowWithInset(bool aInset) const {
    for (const auto& shadow : mBoxShadow.AsSpan()) {
      if (shadow.inset == aInset) {
        return true;
      }
    }
    return false;
  }

  bool HasMixBlendMode() const {
    return mMixBlendMode != mozilla::StyleBlend::Normal;
  }

  bool IsOpaque() const { return mOpacity >= 1.0f; }

  bool IsTransparent() const { return mOpacity == 0.0f; }

  mozilla::StyleOwnedSlice<mozilla::StyleFilter> mFilters;
  mozilla::StyleOwnedSlice<mozilla::StyleBoxShadow> mBoxShadow;
  mozilla::StyleOwnedSlice<mozilla::StyleFilter> mBackdropFilters;
  mozilla::StyleClipRectOrAuto mClip;  // offsets from UL border edge
  float mOpacity;
  mozilla::StyleBlend mMixBlendMode;
};

#undef STYLE_STRUCT

#define STATIC_ASSERT_TYPE_LAYOUTS_MATCH(T1, T2)           \
  static_assert(sizeof(T1) == sizeof(T2),                  \
                "Size mismatch between " #T1 " and " #T2); \
  static_assert(alignof(T1) == alignof(T2),                \
                "Align mismatch between " #T1 " and " #T2);

#define STATIC_ASSERT_FIELD_OFFSET_MATCHES(T1, T2, field)          \
  static_assert(offsetof(T1, field) == offsetof(T2, field),        \
                "Field offset mismatch of " #field " between " #T1 \
                " and " #T2);

/**
 * These *_Simple types are used to map Gecko types to layout-equivalent but
 * simpler Rust types, to aid Rust binding generation.
 *
 * If something in this types or the assertions below needs to change, ask
 * bholley, heycam or emilio before!
 *
 * <div rustbindgen="true" replaces="nsPoint">
 */
struct nsPoint_Simple {
  nscoord x, y;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsPoint, nsPoint_Simple);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsPoint, nsPoint_Simple, x);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsPoint, nsPoint_Simple, y);

/**
 * <div rustbindgen="true" replaces="nsMargin">
 */
struct nsMargin_Simple {
  nscoord top, right, bottom, left;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsMargin, nsMargin_Simple);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsMargin, nsMargin_Simple, top);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsMargin, nsMargin_Simple, right);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsMargin, nsMargin_Simple, bottom);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsMargin, nsMargin_Simple, left);

/**
 * <div rustbindgen="true" replaces="nsRect">
 */
struct nsRect_Simple {
  nscoord x, y, width, height;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsRect, nsRect_Simple);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsRect, nsRect_Simple, x);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsRect, nsRect_Simple, y);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsRect, nsRect_Simple, width);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsRect, nsRect_Simple, height);

/**
 * <div rustbindgen="true" replaces="nsSize">
 */
struct nsSize_Simple {
  nscoord width, height;
};

STATIC_ASSERT_TYPE_LAYOUTS_MATCH(nsSize, nsSize_Simple);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsSize, nsSize_Simple, width);
STATIC_ASSERT_FIELD_OFFSET_MATCHES(nsSize, nsSize_Simple, height);

#endif /* nsStyleStruct_h_ */
