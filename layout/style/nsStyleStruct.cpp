/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * structs that contain the data provided by ComputedStyle, the
 * internal API for computed style data for an element
 */

#include "nsStyleStruct.h"

#include <algorithm>

#include "AnchorPositioningUtils.h"
#include "CounterStyleManager.h"
#include "ImageLoader.h"
#include "imgIContainer.h"
#include "imgIRequest.h"
#include "mozilla/CORSMode.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/GeckoBindings.h"
#include "mozilla/Likely.h"
#include "mozilla/PreferenceSheet.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPresData.h"
#include "mozilla/dom/AnimationEffectBinding.h"    // for PlaybackDirection
#include "mozilla/dom/BaseKeyframeTypesBinding.h"  // for CompositeOperation
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "nsBidiUtils.h"
#include "nsCOMPtr.h"
#include "nsCRTGlue.h"
#include "nsCSSProps.h"
#include "nsContainerFrame.h"
#include "nsDeviceContext.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"
#include "nsIWidget.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsString.h"
#include "nsStyleConsts.h"
#include "nsStyleStructInlines.h"
#include "nsStyleStructList.h"
#include "nsStyleUtil.h"

using namespace mozilla;
using namespace mozilla::dom;

MOZ_RUNINIT static const nscoord kMediumBorderWidth =
    nsPresContext::CSSPixelsToAppUnits(3);

// We set the size limit of style structs to 504 bytes so that when they
// are allocated by Servo side with Arc, the total size doesn't exceed
// 512 bytes, which minimizes allocator slop.
static constexpr size_t kStyleStructSizeLimit = 504;

template <typename Struct, size_t Actual, size_t Limit>
struct AssertSizeIsLessThan {
  static_assert(Actual == sizeof(Struct), "Bogus invocation");
  static_assert(Actual <= Limit,
                "Style struct became larger than the size limit");
  static constexpr bool instantiate = true;
};

#define ASSERT_SIZE(name_)                                                   \
  static_assert(AssertSizeIsLessThan<nsStyle##name_, sizeof(nsStyle##name_), \
                                     kStyleStructSizeLimit>::instantiate,    \
                "");
FOR_EACH_STYLE_STRUCT(ASSERT_SIZE, ASSERT_SIZE)
#undef ASSERT_SIZE

bool StyleCssUrlData::operator==(const StyleCssUrlData& aOther) const {
  // This very intentionally avoids comparing LoadData and such.
  const auto& extra = extra_data.get();
  const auto& otherExtra = aOther.extra_data.get();
  if (extra.BaseURI() != otherExtra.BaseURI() ||
      extra.Principal() != otherExtra.Principal() ||
      cors_mode != aOther.cors_mode) {
    // NOTE(emilio): This does pointer comparison, but it's what URLValue used
    // to do. That's ok though since this is only used for style struct diffing.
    return false;
  }
  return serialization == aOther.serialization;
}

StyleLoadData::~StyleLoadData() { Gecko_LoadData_Drop(this); }

void StyleComputedUrl::ResolveImage(Document& aDocument,
                                    const StyleComputedUrl* aOldImage) {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  StyleLoadData& data = MutLoadData();

  MOZ_ASSERT(!(data.flags & StyleLoadDataFlags::TRIED_TO_RESOLVE_IMAGE));

  data.flags |= StyleLoadDataFlags::TRIED_TO_RESOLVE_IMAGE;

  // TODO(emilio, bug 1440442): This is a hackaround to avoid flickering due the
  // lack of non-http image caching in imagelib (bug 1406134), which causes
  // stuff like bug 1439285. Cleanest fix if that doesn't get fixed is bug
  // 1440305, but that seems too risky, and a lot of work to do before 60.
  //
  // Once that's fixed, the "old style" argument to TriggerImageLoads can go
  // away, and same for mSharedCount in the image loader and so on.
  const bool reuseProxy = nsContentUtils::IsChromeDoc(&aDocument) &&
                          aOldImage && aOldImage->IsImageResolved() &&
                          *this == *aOldImage;

  RefPtr<imgRequestProxy> request;
  if (reuseProxy) {
    request = aOldImage->LoadData().resolved_image;
    if (request) {
      css::ImageLoader::NoteSharedLoad(request);
    }
  } else {
    request = css::ImageLoader::LoadImage(*this, aDocument);
  }

  if (!request) {
    return;
  }

  data.resolved_image = request.forget().take();

  // Boost priority now that we know the image is present in the ComputedStyle
  // of some frame.
  data.resolved_image->BoostPriority(imgIRequest::CATEGORY_FRAME_STYLE);
}

/**
 * Runnable to release the image request's mRequestProxy on the main thread, and
 * to perform any necessary unlocking and untracking of the image.
 */
class StyleImageRequestCleanupTask final : public mozilla::Runnable {
 public:
  explicit StyleImageRequestCleanupTask(StyleLoadData& aData)
      : mozilla::Runnable("StyleImageRequestCleanupTask"),
        mRequestProxy(dont_AddRef(aData.resolved_image)) {
    MOZ_ASSERT(mRequestProxy);
    aData.resolved_image = nullptr;
  }

  NS_IMETHOD Run() final {
    MOZ_ASSERT(NS_IsMainThread());
    css::ImageLoader::UnloadImage(mRequestProxy);
    return NS_OK;
  }

 protected:
  virtual ~StyleImageRequestCleanupTask() {
    MOZ_ASSERT(!mRequestProxy || NS_IsMainThread(),
               "mRequestProxy destructor need to run on the main thread!");
  }

 private:
  // Since we always dispatch this runnable to the main thread, these will be
  // released on the main thread when the runnable itself is released.
  RefPtr<imgRequestProxy> mRequestProxy;
};

// This is defined here for parallelism with LoadURI.
void Gecko_LoadData_Drop(StyleLoadData* aData) {
  if (aData->resolved_image) {
    // We want to dispatch this async to prevent reentrancy issues, as
    // imgRequestProxy going away can destroy documents, etc, see bug 1677555.
    auto task = MakeRefPtr<StyleImageRequestCleanupTask>(*aData);
    SchedulerGroup::Dispatch(task.forget());
  }

  // URIs are safe to refcount from any thread.
  NS_IF_RELEASE(aData->resolved_uri);
}

// --------------------
// nsStyleFont
//
nsStyleFont::nsStyleFont(const nsStyleFont& aSrc)
    : mFont(aSrc.mFont),
      mSize(aSrc.mSize),
      mFontSizeFactor(aSrc.mFontSizeFactor),
      mFontSizeOffset(aSrc.mFontSizeOffset),
      mFontSizeKeyword(aSrc.mFontSizeKeyword),
      mFontPalette(aSrc.mFontPalette),
      mMathDepth(aSrc.mMathDepth),
      mLineHeight(aSrc.mLineHeight),
      mMinFontSizeRatio(aSrc.mMinFontSizeRatio),
      mMathVariant(aSrc.mMathVariant),
      mMathStyle(aSrc.mMathStyle),
      mMathShift(aSrc.mMathShift),
      mExplicitLanguage(aSrc.mExplicitLanguage),
      mXTextScale(aSrc.mXTextScale),
      mScriptUnconstrainedSize(aSrc.mScriptUnconstrainedSize),
      mScriptMinSize(aSrc.mScriptMinSize),
      mLanguage(aSrc.mLanguage) {
  MOZ_COUNT_CTOR(nsStyleFont);
}

static StyleXTextScale InitialTextScale(const Document& aDoc) {
  if (nsContentUtils::IsChromeDoc(&aDoc) ||
      nsContentUtils::IsPDFJS(aDoc.NodePrincipal())) {
    return StyleXTextScale::ZoomOnly;
  }
  return StyleXTextScale::All;
}

nsStyleFont::nsStyleFont(const Document& aDocument)
    : mFont(*aDocument.GetFontPrefsForLang(nullptr)->GetDefaultFont(
          StyleGenericFontFamily::None)),
      mSize(ZoomText(aDocument, mFont.size)),
      mFontSizeFactor(1.0),
      mFontSizeOffset{0},
      mFontSizeKeyword(StyleFontSizeKeyword::Medium),
      mFontPalette(StyleFontPalette::Normal()),
      mMathDepth(0),
      mLineHeight(StyleLineHeight::Normal()),
      mMathVariant(StyleMathVariant::None),
      mMathStyle(StyleMathStyle::Normal),
      mMathShift(StyleMathShift::Normal),
      mXTextScale(InitialTextScale(aDocument)),
      mScriptUnconstrainedSize(mSize),
      mScriptMinSize(Length::FromPixels(
          CSSPixel::FromPoints(kMathMLDefaultScriptMinSizePt))),
      mLanguage(aDocument.GetLanguageForStyle()) {
  MOZ_COUNT_CTOR(nsStyleFont);
  MOZ_ASSERT(NS_IsMainThread());
  mFont.family.is_initial = true;
  mFont.size = mSize;
  if (MinFontSizeEnabled()) {
    const Length minimumFontSize =
        aDocument.GetFontPrefsForLang(mLanguage)->mMinimumFontSize;
    mFont.size = Length::FromPixels(
        std::max(mSize.ToCSSPixels(), minimumFontSize.ToCSSPixels()));
  }
}

nsChangeHint nsStyleFont::CalcDifference(const nsStyleFont& aNewData) const {
  MOZ_ASSERT(mXTextScale == aNewData.mXTextScale,
             "expected -x-text-scale to be the same on both nsStyleFonts");
  if (mSize != aNewData.mSize || mLanguage != aNewData.mLanguage ||
      mExplicitLanguage != aNewData.mExplicitLanguage ||
      mMathVariant != aNewData.mMathVariant ||
      mMathStyle != aNewData.mMathStyle || mMathShift != aNewData.mMathShift ||
      mMinFontSizeRatio != aNewData.mMinFontSizeRatio ||
      mLineHeight != aNewData.mLineHeight) {
    return NS_STYLE_HINT_REFLOW;
  }

  switch (mFont.CalcDifference(aNewData.mFont)) {
    case nsFont::MaxDifference::eLayoutAffecting:
      return NS_STYLE_HINT_REFLOW;

    case nsFont::MaxDifference::eVisual:
      return NS_STYLE_HINT_VISUAL;

    case nsFont::MaxDifference::eNone:
      break;
  }

  if (mFontPalette != aNewData.mFontPalette) {
    return NS_STYLE_HINT_VISUAL;
  }

  // XXX Should any of these cause a non-nsChangeHint_NeutralChange change?
  if (mMathDepth != aNewData.mMathDepth ||
      mScriptUnconstrainedSize != aNewData.mScriptUnconstrainedSize ||
      mScriptMinSize != aNewData.mScriptMinSize) {
    return nsChangeHint_NeutralChange;
  }

  return nsChangeHint(0);
}

Length nsStyleFont::ZoomText(const Document& aDocument, Length aSize) {
  if (auto* pc = aDocument.GetPresContext()) {
    aSize.ScaleBy(pc->TextZoom());
  }
  return aSize;
}

template <typename T>
static StyleRect<T> StyleRectWithAllSides(const T& aSide) {
  return {aSide, aSide, aSide, aSide};
}

bool AnchorPosResolutionParams::AutoResolutionOverrideParams::OverriddenToZero(
    StylePhysicalAxis aAxis) const {
  if (mPositionAreaInUse) {
    // If `position-area` is used "Any auto inset properties resolve to 0":
    // https://drafts.csswg.org/css-anchor-position-1/#valdef-position-area-position-area
    return true;
  }

  // If `anchor-center` is used with a valid anchor, "auto inset
  // properties resolve to 0" on that axis:
  // https://drafts.csswg.org/css-anchor-position-1/#anchor-center
  if (aAxis == StylePhysicalAxis::Vertical) {
    return mVAnchorCenter;
  }
  MOZ_ASSERT(aAxis == StylePhysicalAxis::Horizontal);
  return mHAnchorCenter;
}

static AnchorPosResolutionParams::AutoResolutionOverrideParams
GetAutoResolutionOverrideParams(const nsIFrame* aFrame,
                                bool aDefaultAnchorValid) {
  if (!aFrame) {
    return {};
  }
  nsIFrame* parent = aFrame->GetParent();
  if (!parent || !aFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) ||
      !aDefaultAnchorValid) {
    return {};
  }

  const auto* stylePos = aFrame->StylePosition();
  const auto cbwm = parent->GetWritingMode();

  auto checkAxis = [&](LogicalAxis aAxis) {
    StyleAlignFlags alignment =
        stylePos->UsedSelfAlignment(aAxis, parent->Style());
    return (alignment & ~StyleAlignFlags::FLAG_BITS) ==
           StyleAlignFlags::ANCHOR_CENTER;
  };

  const auto horizontalLogicalAxis =
      cbwm.IsVertical() ? LogicalAxis::Block : LogicalAxis::Inline;
  AnchorPosResolutionParams::AutoResolutionOverrideParams result;
  result.mHAnchorCenter = checkAxis(horizontalLogicalAxis);
  result.mVAnchorCenter = checkAxis(GetOrthogonalAxis(horizontalLogicalAxis));
  result.mPositionAreaInUse = !stylePos->mPositionArea.IsNone();
  return result;
}

AnchorPosResolutionParams::AutoResolutionOverrideParams::
    AutoResolutionOverrideParams(
        const nsIFrame* aFrame, const mozilla::AnchorPosResolutionCache* aCache)
    : AutoResolutionOverrideParams{GetAutoResolutionOverrideParams(
          aFrame, aCache && aCache->mDefaultAnchorCache.mAnchor)} {}

AnchorPosResolutionParams::AutoResolutionOverrideParams::
    AutoResolutionOverrideParams(const nsIFrame* aFrame)
    : AutoResolutionOverrideParams{
          GetAutoResolutionOverrideParams(aFrame, [&]() {
            if (!aFrame) {
              return false;
            }
            const auto* references =
                aFrame->GetProperty(nsIFrame::AnchorPosReferences());
            if (!references || !references->mDefaultAnchorName) {
              // It is presumed that this is called on a reflowed frame.
              return false;
            }
            const auto* entry = references->Lookup(
                {references->mDefaultAnchorName, references->mAnchorTreeScope});
            return entry && entry->isSome();
          }())} {}

AnchorResolvedMargin AnchorResolvedMarginHelper::ResolveAnchor(
    const StyleMargin& aValue, StylePhysicalAxis aAxis,
    const AnchorPosResolutionParams& aParams) {
  MOZ_ASSERT(aValue.HasAnchorPositioningFunction(),
             "Calling anchor resolution without using it?");
  if (aValue.IsAnchorSizeFunction()) {
    auto resolved = StyleAnchorPositioningFunctionResolution::Invalid();
    Servo_ResolveAnchorSizeFunctionForMargin(&*aValue.AsAnchorSizeFunction(),
                                             &aParams, aAxis, &resolved);
    if (resolved.IsInvalid()) {
      return Zero();
    }
    if (resolved.IsResolvedReference()) {
      return MakeUniqueOfUniqueOrNonOwning<const StyleMargin>(
          *resolved.AsResolvedReference());
    }
    return MakeUniqueOfUniqueOrNonOwning<const StyleMargin>(
        resolved.AsResolved());
  }

  const auto& lp = aValue.AsAnchorContainingCalcFunction();
  const auto& c = lp.AsCalc();
  auto result = StyleCalcAnchorPositioningFunctionResolution::Invalid();
  AnchorPosOffsetResolutionParams params =
      AnchorPosOffsetResolutionParams::UseCBFrameSize(aParams);
  const auto allowed =
      StyleAllowAnchorPosResolutionInCalcPercentage::AnchorSizeOnly(aAxis);
  Servo_ResolveAnchorFunctionsInCalcPercentage(&c, &allowed, &params, &result);
  if (result.IsInvalid()) {
    return Zero();
  }
  return MakeUniqueOfUniqueOrNonOwning<const StyleMargin>(result.AsValid());
}

nsStyleMargin::nsStyleMargin()
    : mMargin(StyleRectWithAllSides(
          StyleMargin::LengthPercentage(LengthPercentage::Zero()))),
      mScrollMargin(StyleRectWithAllSides(StyleLength{0.})),
      mOverflowClipMargin(
          {StyleLength::Zero(), StyleOverflowClipMarginBox::PaddingBox}) {
  MOZ_COUNT_CTOR(nsStyleMargin);
}

nsStyleMargin::nsStyleMargin(const nsStyleMargin& aSrc)
    : mMargin(aSrc.mMargin),
      mScrollMargin(aSrc.mScrollMargin),
      mOverflowClipMargin(aSrc.mOverflowClipMargin) {
  MOZ_COUNT_CTOR(nsStyleMargin);
}

nsChangeHint nsStyleMargin::CalcDifference(
    const nsStyleMargin& aNewData) const {
  nsChangeHint hint = nsChangeHint(0);

  if (!MarginEquals(aNewData)) {
    // Margin differences can't affect descendant intrinsic sizes and
    // don't need to force children to reflow.
    hint |= nsChangeHint_NeedReflow | nsChangeHint_ReflowChangesSizeOrPosition |
            nsChangeHint_ClearAncestorIntrinsics;
  }

  if (mScrollMargin != aNewData.mScrollMargin) {
    // FIXME: Bug 1530253 Support re-snapping when scroll-margin changes.
    hint |= nsChangeHint_NeutralChange;
  }

  if (mOverflowClipMargin != aNewData.mOverflowClipMargin) {
    hint |= nsChangeHint_UpdateOverflow | nsChangeHint_RepaintFrame;
  }

  return hint;
}

nsStylePadding::nsStylePadding()
    : mPadding(StyleRectWithAllSides(LengthPercentage::Zero())),
      mScrollPadding(StyleRectWithAllSides(LengthPercentageOrAuto::Auto())) {
  MOZ_COUNT_CTOR(nsStylePadding);
}

nsStylePadding::nsStylePadding(const nsStylePadding& aSrc)
    : mPadding(aSrc.mPadding), mScrollPadding(aSrc.mScrollPadding) {
  MOZ_COUNT_CTOR(nsStylePadding);
}

nsChangeHint nsStylePadding::CalcDifference(
    const nsStylePadding& aNewData) const {
  nsChangeHint hint = nsChangeHint(0);

  if (mPadding != aNewData.mPadding) {
    // Padding differences can't affect descendant intrinsic sizes, but do need
    // to force children to reflow so that we can reposition them, since their
    // offsets are from our frame bounds but our content rect's position within
    // those bounds is moving.
    // FIXME: It would be good to return a weaker hint here that doesn't
    // force reflow of all descendants, but the hint would need to force
    // reflow of the frame's children (see how
    // ReflowInput::InitResizeFlags initializes the inline-resize flag).
    hint |= NS_STYLE_HINT_REFLOW & ~nsChangeHint_ClearDescendantIntrinsics;
  }

  if (mScrollPadding != aNewData.mScrollPadding) {
    // FIXME: Bug 1530253 Support re-snapping when scroll-padding changes.
    hint |= nsChangeHint_NeutralChange;
  }

  return hint;
}

static inline BorderRadius ZeroBorderRadius() {
  auto zero = LengthPercentage::Zero();
  return {{{zero, zero}}, {{zero, zero}}, {{zero, zero}}, {{zero, zero}}};
}

nsStyleBorder::nsStyleBorder()
    : mBorderRadius(ZeroBorderRadius()),
      mBorderImageSource(StyleImage::None()),
      mBorderImageWidth(
          StyleRectWithAllSides(StyleBorderImageSideWidth::Number(1.))),
      mBorderImageOutset(
          StyleRectWithAllSides(StyleNonNegativeLengthOrNumber::Number(0.))),
      mBorderImageSlice(
          {StyleRectWithAllSides(StyleNumberOrPercentage::Percentage({1.})),
           false}),
      mBorderImageRepeat{StyleBorderImageRepeatKeyword::Stretch,
                         StyleBorderImageRepeatKeyword::Stretch},
      mFloatEdge(StyleFloatEdge::ContentBox),
      mBoxDecorationBreak(StyleBoxDecorationBreak::Slice),
      mBorderStyle(StyleRectWithAllSides(StyleBorderStyle::None)),
      mBorder(StyleRectWithAllSides(kMediumBorderWidth)),
      mBorderTopColor(StyleColor::CurrentColor()),
      mBorderRightColor(StyleColor::CurrentColor()),
      mBorderBottomColor(StyleColor::CurrentColor()),
      mBorderLeftColor(StyleColor::CurrentColor()) {
  MOZ_COUNT_CTOR(nsStyleBorder);
}

nsStyleBorder::nsStyleBorder(const nsStyleBorder& aSrc)
    : mBorderRadius(aSrc.mBorderRadius),
      mBorderImageSource(aSrc.mBorderImageSource),
      mBorderImageWidth(aSrc.mBorderImageWidth),
      mBorderImageOutset(aSrc.mBorderImageOutset),
      mBorderImageSlice(aSrc.mBorderImageSlice),
      mBorderImageRepeat(aSrc.mBorderImageRepeat),
      mFloatEdge(aSrc.mFloatEdge),
      mBoxDecorationBreak(aSrc.mBoxDecorationBreak),
      mBorderStyle(aSrc.mBorderStyle),
      mBorder(aSrc.mBorder),
      mBorderTopColor(aSrc.mBorderTopColor),
      mBorderRightColor(aSrc.mBorderRightColor),
      mBorderBottomColor(aSrc.mBorderBottomColor),
      mBorderLeftColor(aSrc.mBorderLeftColor) {
  MOZ_COUNT_CTOR(nsStyleBorder);
}

void nsStyleBorder::TriggerImageLoads(Document& aDocument,
                                      const nsStyleBorder* aOldStyle) {
  MOZ_ASSERT(NS_IsMainThread());

  mBorderImageSource.ResolveImage(
      aDocument, aOldStyle ? &aOldStyle->mBorderImageSource : nullptr);
}

nsMargin nsStyleBorder::GetImageOutset() const {
  // We don't check whether there is a border-image (which is OK since
  // the initial values yields 0 outset) so that we don't have to
  // reflow to update overflow areas when an image loads.
  nsMargin outset;
  auto computedBorder = GetComputedBorder();
  for (const auto s : mozilla::AllPhysicalSides()) {
    const auto& coord = mBorderImageOutset.Get(s);
    nscoord value;
    if (coord.IsLength()) {
      value = coord.AsLength().ToAppUnits();
    } else {
      MOZ_ASSERT(coord.IsNumber());
      value = coord.AsNumber() * computedBorder.Side(s);
    }
    outset.Side(s) = value;
  }
  return outset;
}

nsChangeHint nsStyleBorder::CalcDifference(
    const nsStyleBorder& aNewData) const {
  // FIXME: XXXbz: As in nsStylePadding::CalcDifference, many of these
  // differences should not need to clear descendant intrinsics.
  // FIXME: It would be good to return a weaker hint for the
  // GetComputedBorder() differences (and perhaps others) that doesn't
  // force reflow of all descendants, but the hint would need to force
  // reflow of the frame's children (see how
  // ReflowInput::InitResizeFlags initializes the inline-resize flag).
  if (GetComputedBorder() != aNewData.GetComputedBorder() ||
      mFloatEdge != aNewData.mFloatEdge ||
      mBorderImageOutset != aNewData.mBorderImageOutset ||
      mBoxDecorationBreak != aNewData.mBoxDecorationBreak) {
    return NS_STYLE_HINT_REFLOW;
  }

  for (const auto ix : mozilla::AllPhysicalSides()) {
    if (mBorderStyle.Get(ix) != aNewData.mBorderStyle.Get(ix) ||
        BorderColorFor(ix) != aNewData.BorderColorFor(ix)) {
      return nsChangeHint_RepaintFrame;
    }
  }

  // Note that border radius also controls the outline radius if the
  // layout.css.outline-follows-border-radius.enabled pref is set. Any
  // optimizations here should apply to both.
  if (mBorderRadius != aNewData.mBorderRadius) {
    return nsChangeHint_RepaintFrame;
  }

  // Loading status of the border image can be accessed in main thread only
  // while CalcDifference might be executed on a background thread. As a
  // result, we have to check mBorderImage* fields even before border image was
  // actually loaded.
  if (!mBorderImageSource.IsNone() || !aNewData.mBorderImageSource.IsNone()) {
    if (mBorderImageSource != aNewData.mBorderImageSource ||
        mBorderImageRepeat != aNewData.mBorderImageRepeat ||
        mBorderImageSlice != aNewData.mBorderImageSlice ||
        mBorderImageWidth != aNewData.mBorderImageWidth) {
      return nsChangeHint_RepaintFrame;
    }
  }

  // mBorder is the specified border value.  Changes to this don't
  // need any change processing, since we operate on the computed
  // border values instead.
  // mBorderImage* fields are checked only when border-image is not 'none'.
  if (mBorder != aNewData.mBorder ||
      mBorderImageSource != aNewData.mBorderImageSource ||
      mBorderImageRepeat != aNewData.mBorderImageRepeat ||
      mBorderImageSlice != aNewData.mBorderImageSlice ||
      mBorderImageWidth != aNewData.mBorderImageWidth) {
    return nsChangeHint_NeutralChange;
  }

  return nsChangeHint(0);
}

nsStyleOutline::nsStyleOutline()
    : mOutlineWidth(kMediumBorderWidth),
      mOutlineOffset(0),
      mOutlineColor(StyleColor::CurrentColor()),
      mOutlineStyle(StyleOutlineStyle::BorderStyle(StyleBorderStyle::None)) {
  MOZ_COUNT_CTOR(nsStyleOutline);
}

nsStyleOutline::nsStyleOutline(const nsStyleOutline& aSrc)
    : mOutlineWidth(aSrc.mOutlineWidth),
      mOutlineOffset(aSrc.mOutlineOffset),
      mOutlineColor(aSrc.mOutlineColor),
      mOutlineStyle(aSrc.mOutlineStyle) {
  MOZ_COUNT_CTOR(nsStyleOutline);
}

nsChangeHint nsStyleOutline::CalcDifference(
    const nsStyleOutline& aNewData) const {
  const bool shouldPaintOutline = ShouldPaintOutline();
  // We need the explicit 'outline-style: auto' check because
  // 'outline-style: auto' effectively also changes 'outline-width'.
  if (shouldPaintOutline != aNewData.ShouldPaintOutline() ||
      mOutlineWidth != aNewData.mOutlineWidth ||
      mOutlineStyle.IsAuto() != aNewData.mOutlineStyle.IsAuto() ||
      (shouldPaintOutline && mOutlineOffset != aNewData.mOutlineOffset)) {
    return nsChangeHint_UpdateOverflow | nsChangeHint_SchedulePaint |
           nsChangeHint_RepaintFrame;
  }

  if (mOutlineStyle != aNewData.mOutlineStyle ||
      mOutlineColor != aNewData.mOutlineColor) {
    return shouldPaintOutline ? nsChangeHint_RepaintFrame
                              : nsChangeHint_NeutralChange;
  }

  if (mOutlineWidth != aNewData.mOutlineWidth ||
      mOutlineOffset != aNewData.mOutlineOffset) {
    return nsChangeHint_NeutralChange;
  }

  return nsChangeHint(0);
}

nsSize nsStyleOutline::EffectiveOffsetFor(const nsRect& aRect) const {
  const nscoord offset = mOutlineOffset;
  if (offset >= 0) {
    // Fast path for non-negative offset values
    return nsSize(offset, offset);
  }

  return nsSize(std::max(offset, -(aRect.Width() / 2)),
                std::max(offset, -(aRect.Height() / 2)));
}

// --------------------
// nsStyleList
//
nsStyleList::nsStyleList()
    : mListStylePosition(StyleListStylePosition::Outside),
      mListStyleType(StyleCounterStyle::Name({StyleAtom(nsGkAtoms::disc)})),
      mQuotes(StyleQuotes::Auto()),
      mListStyleImage(StyleImage::None()) {
  MOZ_COUNT_CTOR(nsStyleList);
  MOZ_ASSERT(NS_IsMainThread());
}

nsStyleList::nsStyleList(const nsStyleList& aSource)
    : mListStylePosition(aSource.mListStylePosition),
      mListStyleType(aSource.mListStyleType),
      mQuotes(aSource.mQuotes),
      mListStyleImage(aSource.mListStyleImage) {
  MOZ_COUNT_CTOR(nsStyleList);
}

void nsStyleList::TriggerImageLoads(Document& aDocument,
                                    const nsStyleList* aOldStyle) {
  MOZ_ASSERT(NS_IsMainThread());
  mListStyleImage.ResolveImage(
      aDocument, aOldStyle ? &aOldStyle->mListStyleImage : nullptr);
}

nsChangeHint nsStyleList::CalcDifference(const nsStyleList& aNewData,
                                         const ComputedStyle& aOldStyle) const {
  // If the quotes implementation is ever going to change we might not need
  // a framechange here and a reflow should be sufficient.  See bug 35768.
  if (mQuotes != aNewData.mQuotes) {
    return nsChangeHint_ReconstructFrame;
  }
  nsChangeHint hint = nsChangeHint(0);
  // Only elements whose display value is list-item can be affected by
  // list-style-{position,type,image}. This also relies on that when the display
  // value changes from something else to list-item, that change itself would
  // cause ReconstructFrame.
  if (mListStylePosition != aNewData.mListStylePosition ||
      mListStyleType != aNewData.mListStyleType ||
      mListStyleImage != aNewData.mListStyleImage) {
    if (aOldStyle.StyleDisplay()->IsListItem()) {
      return nsChangeHint_ReconstructFrame;
    }
    // list-style-image may affect nsImageFrame for XUL elements, but that is
    // dealt with explicitly in nsImageFrame::DidSetComputedStyle.
    hint = nsChangeHint_NeutralChange;
  }
  return hint;
}

// --------------------
// nsStyleXUL
//
nsStyleXUL::nsStyleXUL()
    : mBoxFlex(0.0f),
      mBoxOrdinal(1),
      mBoxAlign(StyleBoxAlign::Stretch),
      mBoxDirection(StyleBoxDirection::Normal),
      mBoxOrient(StyleBoxOrient::Horizontal),
      mBoxPack(StyleBoxPack::Start) {
  MOZ_COUNT_CTOR(nsStyleXUL);
}

nsStyleXUL::nsStyleXUL(const nsStyleXUL& aSource)
    : mBoxFlex(aSource.mBoxFlex),
      mBoxOrdinal(aSource.mBoxOrdinal),
      mBoxAlign(aSource.mBoxAlign),
      mBoxDirection(aSource.mBoxDirection),
      mBoxOrient(aSource.mBoxOrient),
      mBoxPack(aSource.mBoxPack) {
  MOZ_COUNT_CTOR(nsStyleXUL);
}

nsChangeHint nsStyleXUL::CalcDifference(const nsStyleXUL& aNewData) const {
  if (mBoxAlign == aNewData.mBoxAlign &&
      mBoxDirection == aNewData.mBoxDirection &&
      mBoxFlex == aNewData.mBoxFlex && mBoxOrient == aNewData.mBoxOrient &&
      mBoxPack == aNewData.mBoxPack && mBoxOrdinal == aNewData.mBoxOrdinal) {
    return nsChangeHint(0);
  }
  if (mBoxOrdinal != aNewData.mBoxOrdinal) {
    return nsChangeHint_ReconstructFrame;
  }
  return NS_STYLE_HINT_REFLOW;
}

// --------------------
// nsStyleColumn
//

nsStyleColumn::nsStyleColumn()
    : mColumnWidth(LengthOrAuto::Auto()),
      mColumnRuleColor(StyleColor::CurrentColor()),
      mColumnRuleStyle(StyleBorderStyle::None),
      mColumnRuleWidth(kMediumBorderWidth) {
  MOZ_COUNT_CTOR(nsStyleColumn);
}

nsStyleColumn::nsStyleColumn(const nsStyleColumn& aSource)
    : mColumnCount(aSource.mColumnCount),
      mColumnWidth(aSource.mColumnWidth),
      mColumnRuleColor(aSource.mColumnRuleColor),
      mColumnRuleStyle(aSource.mColumnRuleStyle),
      mColumnFill(aSource.mColumnFill),
      mColumnSpan(aSource.mColumnSpan),
      mColumnRuleWidth(aSource.mColumnRuleWidth) {
  MOZ_COUNT_CTOR(nsStyleColumn);
}

nsChangeHint nsStyleColumn::CalcDifference(
    const nsStyleColumn& aNewData) const {
  if (mColumnWidth.IsAuto() != aNewData.mColumnWidth.IsAuto() ||
      mColumnCount != aNewData.mColumnCount ||
      mColumnSpan != aNewData.mColumnSpan) {
    // We force column count changes to do a reframe, because it's tricky to
    // handle some edge cases where the column count gets smaller and content
    // overflows.
    // XXX not ideal
    return nsChangeHint_ReconstructFrame;
  }

  if (mColumnWidth != aNewData.mColumnWidth ||
      mColumnFill != aNewData.mColumnFill) {
    return NS_STYLE_HINT_REFLOW;
  }

  if (mColumnRuleWidth != aNewData.mColumnRuleWidth ||
      mColumnRuleStyle != aNewData.mColumnRuleStyle ||
      mColumnRuleColor != aNewData.mColumnRuleColor) {
    return NS_STYLE_HINT_VISUAL;
  }

  return nsChangeHint(0);
}

using SVGPaintFallback = StyleGenericSVGPaintFallback<StyleColor>;

// --------------------
// nsStyleSVG
//
nsStyleSVG::nsStyleSVG()
    : mFill{StyleSVGPaintKind::Color(StyleColor::Black()),
            SVGPaintFallback::Unset()},
      mStroke{StyleSVGPaintKind::None(), SVGPaintFallback::Unset()},
      mMarkerEnd(StyleUrlOrNone::None()),
      mMarkerMid(StyleUrlOrNone::None()),
      mMarkerStart(StyleUrlOrNone::None()),
      mMozContextProperties{{}, {0}},
      mStrokeDasharray(StyleSVGStrokeDashArray::Values({})),
      mStrokeDashoffset(
          StyleSVGLength::LengthPercentage(LengthPercentage::Zero())),
      mStrokeWidth(
          StyleSVGWidth::LengthPercentage(LengthPercentage::FromPixels(1.0f))),
      mFillOpacity(StyleSVGOpacity::Opacity(1.0f)),
      mStrokeMiterlimit(4.0f),
      mStrokeOpacity(StyleSVGOpacity::Opacity(1.0f)),
      mClipRule(StyleFillRule::Nonzero),
      mColorInterpolation(StyleColorInterpolation::Srgb),
      mColorInterpolationFilters(StyleColorInterpolation::Linearrgb),
      mFillRule(StyleFillRule::Nonzero),
      mPaintOrder(0),
      mShapeRendering(StyleShapeRendering::Auto),
      mStrokeLinecap(StyleStrokeLinecap::Butt),
      mStrokeLinejoin(StyleStrokeLinejoin::Miter),
      mTextAnchor(StyleTextAnchor::Start) {
  MOZ_COUNT_CTOR(nsStyleSVG);
}

nsStyleSVG::nsStyleSVG(const nsStyleSVG& aSource)
    : mFill(aSource.mFill),
      mStroke(aSource.mStroke),
      mMarkerEnd(aSource.mMarkerEnd),
      mMarkerMid(aSource.mMarkerMid),
      mMarkerStart(aSource.mMarkerStart),
      mMozContextProperties(aSource.mMozContextProperties),
      mStrokeDasharray(aSource.mStrokeDasharray),
      mStrokeDashoffset(aSource.mStrokeDashoffset),
      mStrokeWidth(aSource.mStrokeWidth),
      mFillOpacity(aSource.mFillOpacity),
      mStrokeMiterlimit(aSource.mStrokeMiterlimit),
      mStrokeOpacity(aSource.mStrokeOpacity),
      mClipRule(aSource.mClipRule),
      mColorInterpolation(aSource.mColorInterpolation),
      mColorInterpolationFilters(aSource.mColorInterpolationFilters),
      mFillRule(aSource.mFillRule),
      mPaintOrder(aSource.mPaintOrder),
      mShapeRendering(aSource.mShapeRendering),
      mStrokeLinecap(aSource.mStrokeLinecap),
      mStrokeLinejoin(aSource.mStrokeLinejoin),
      mTextAnchor(aSource.mTextAnchor) {
  MOZ_COUNT_CTOR(nsStyleSVG);
}

static bool PaintURIChanged(const StyleSVGPaint& aPaint1,
                            const StyleSVGPaint& aPaint2) {
  if (aPaint1.kind.IsPaintServer() != aPaint2.kind.IsPaintServer()) {
    return true;
  }
  return aPaint1.kind.IsPaintServer() &&
         aPaint1.kind.AsPaintServer() != aPaint2.kind.AsPaintServer();
}

nsChangeHint nsStyleSVG::CalcDifference(const nsStyleSVG& aNewData) const {
  nsChangeHint hint = nsChangeHint(0);

  if (mMarkerEnd != aNewData.mMarkerEnd || mMarkerMid != aNewData.mMarkerMid ||
      mMarkerStart != aNewData.mMarkerStart) {
    // Markers currently contribute to SVGGeometryFrame::mRect,
    // so we need a reflow as well as a repaint. No intrinsic sizes need
    // to change, so nsChangeHint_NeedReflow is sufficient.
    return nsChangeHint_UpdateEffects | nsChangeHint_NeedReflow |
           nsChangeHint_RepaintFrame;
  }

  if (mFill != aNewData.mFill || mStroke != aNewData.mStroke ||
      mFillOpacity != aNewData.mFillOpacity ||
      mStrokeOpacity != aNewData.mStrokeOpacity) {
    hint |= nsChangeHint_RepaintFrame;
    if (HasStroke() != aNewData.HasStroke() ||
        (!HasStroke() && HasFill() != aNewData.HasFill())) {
      // Frame bounds and overflow rects depend on whether we "have" fill or
      // stroke. Whether we have stroke or not just changed, or else we have no
      // stroke (in which case whether we have fill or not is significant to
      // frame bounds) and whether we have fill or not just changed. In either
      // case we need to reflow so the frame rect is updated.
      // XXXperf this is a waste on non SVGGeometryFrames.
      hint |= nsChangeHint_NeedReflow;
    }
    if (PaintURIChanged(mFill, aNewData.mFill) ||
        PaintURIChanged(mStroke, aNewData.mStroke)) {
      hint |= nsChangeHint_UpdateEffects;
    }
  }

  // Stroke currently contributes to SVGGeometryFrame::mRect, so
  // we need a reflow here. No intrinsic sizes need to change, so
  // nsChangeHint_NeedReflow is sufficient.
  // Note that stroke-dashoffset does not affect SVGGeometryFrame::mRect.
  // text-anchor and dominant-baseline changes also require a reflow since
  // they change frames' rects.
  if (mStrokeWidth != aNewData.mStrokeWidth ||
      mStrokeMiterlimit != aNewData.mStrokeMiterlimit ||
      mStrokeLinecap != aNewData.mStrokeLinecap ||
      mStrokeLinejoin != aNewData.mStrokeLinejoin ||
      mTextAnchor != aNewData.mTextAnchor) {
    return hint | nsChangeHint_NeedReflow | nsChangeHint_RepaintFrame;
  }

  if (hint & nsChangeHint_RepaintFrame) {
    return hint;  // we don't add anything else below
  }

  if (mStrokeDashoffset != aNewData.mStrokeDashoffset ||
      mClipRule != aNewData.mClipRule ||
      mColorInterpolation != aNewData.mColorInterpolation ||
      mColorInterpolationFilters != aNewData.mColorInterpolationFilters ||
      mFillRule != aNewData.mFillRule || mPaintOrder != aNewData.mPaintOrder ||
      mShapeRendering != aNewData.mShapeRendering ||
      mStrokeDasharray != aNewData.mStrokeDasharray ||
      mMozContextProperties.bits != aNewData.mMozContextProperties.bits) {
    return hint | nsChangeHint_RepaintFrame;
  }

  if (!hint) {
    if (mMozContextProperties.idents != aNewData.mMozContextProperties.idents) {
      hint = nsChangeHint_NeutralChange;
    }
  }

  return hint;
}

// --------------------
// nsStyleSVGReset
//
nsStyleSVGReset::nsStyleSVGReset()
    : mX(LengthPercentage::Zero()),
      mY(LengthPercentage::Zero()),
      mCx(LengthPercentage::Zero()),
      mCy(LengthPercentage::Zero()),
      mRx(NonNegativeLengthPercentageOrAuto::Auto()),
      mRy(NonNegativeLengthPercentageOrAuto::Auto()),
      mR(NonNegativeLengthPercentage::Zero()),
      mMask(nsStyleImageLayers::LayerType::Mask),
      mClipPath(StyleClipPath::None()),
      mStopColor(StyleColor::Black()),
      mFloodColor(StyleColor::Black()),
      mLightingColor(StyleColor::White()),
      mStopOpacity(1.0f),
      mFloodOpacity(1.0f),
      mVectorEffect(StyleVectorEffect::NONE),
      mMaskType(StyleMaskType::Luminance),
      mD(StyleDProperty::None()) {
  MOZ_COUNT_CTOR(nsStyleSVGReset);
}

nsStyleSVGReset::nsStyleSVGReset(const nsStyleSVGReset& aSource)
    : mX(aSource.mX),
      mY(aSource.mY),
      mCx(aSource.mCx),
      mCy(aSource.mCy),
      mRx(aSource.mRx),
      mRy(aSource.mRy),
      mR(aSource.mR),
      mMask(aSource.mMask),
      mClipPath(aSource.mClipPath),
      mStopColor(aSource.mStopColor),
      mFloodColor(aSource.mFloodColor),
      mLightingColor(aSource.mLightingColor),
      mStopOpacity(aSource.mStopOpacity),
      mFloodOpacity(aSource.mFloodOpacity),
      mVectorEffect(aSource.mVectorEffect),
      mMaskType(aSource.mMaskType),
      mD(aSource.mD) {
  MOZ_COUNT_CTOR(nsStyleSVGReset);
}

void nsStyleSVGReset::TriggerImageLoads(Document& aDocument,
                                        const nsStyleSVGReset* aOldStyle) {
  MOZ_ASSERT(NS_IsMainThread());
  // NOTE(emilio): we intentionally don't call TriggerImageLoads for clip-path.

  NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(i, mMask) {
    auto& image = mMask.mLayers[i].mImage;
    if (!image.IsImageRequestType()) {
      continue;
    }
    const auto* url = image.GetImageRequestURLValue();
    // If the url is a local ref, it must be a <mask-resource>, so we don't
    // need to resolve the style image.
    if (url->IsLocalRef()) {
      continue;
    }
#if 0
    // XXX The old style system also checks whether this is a reference to
    // the current document with reference, but it doesn't seem to be a
    // behavior mentioned anywhere, so we comment out the code for now.
    nsIURI* docURI = aPresContext->Document()->GetDocumentURI();
    if (url->EqualsExceptRef(docURI)) {
      continue;
    }
#endif
    // Otherwise, we may need the image even if it has a reference, in case
    // the referenced element isn't a valid SVG <mask> element.
    const auto* oldImage = (aOldStyle && aOldStyle->mMask.mLayers.Length() > i)
                               ? &aOldStyle->mMask.mLayers[i].mImage
                               : nullptr;

    image.ResolveImage(aDocument, oldImage);
  }
}

nsChangeHint nsStyleSVGReset::CalcDifference(
    const nsStyleSVGReset& aNewData) const {
  nsChangeHint hint = nsChangeHint(0);

  if (mX != aNewData.mX || mY != aNewData.mY || mCx != aNewData.mCx ||
      mCy != aNewData.mCy || mR != aNewData.mR || mRx != aNewData.mRx ||
      mRy != aNewData.mRy || mD != aNewData.mD) {
    hint |= nsChangeHint_InvalidateRenderingObservers | nsChangeHint_NeedReflow;
  }

  if (mClipPath != aNewData.mClipPath) {
    hint |= nsChangeHint_UpdateEffects | nsChangeHint_RepaintFrame;
  }

  if (mVectorEffect != aNewData.mVectorEffect) {
    // Stroke currently affects SVGGeometryFrame::mRect, and
    // vector-effect affect stroke. As a result we need to reflow if
    // vector-effect changes in order to have SVGGeometryFrame::
    // ReflowSVG called to update its mRect. No intrinsic sizes need
    // to change so nsChangeHint_NeedReflow is sufficient.
    hint |= nsChangeHint_NeedReflow | nsChangeHint_RepaintFrame;
  } else if (mStopColor != aNewData.mStopColor ||
             mFloodColor != aNewData.mFloodColor ||
             mLightingColor != aNewData.mLightingColor ||
             mStopOpacity != aNewData.mStopOpacity ||
             mFloodOpacity != aNewData.mFloodOpacity ||
             mMaskType != aNewData.mMaskType || mD != aNewData.mD) {
    hint |= nsChangeHint_RepaintFrame;
  }

  hint |=
      mMask.CalcDifference(aNewData.mMask, nsStyleImageLayers::LayerType::Mask);

  return hint;
}

bool nsStyleSVGReset::HasMask() const {
  for (uint32_t i = 0; i < mMask.mImageCount; i++) {
    if (!mMask.mLayers[i].mImage.IsNone()) {
      return true;
    }
  }

  return false;
}

// --------------------
// nsStylePage
//

nsStylePage::nsStylePage(const nsStylePage& aSrc)
    : mSize(aSrc.mSize),
      mPage(aSrc.mPage),
      mPageOrientation(aSrc.mPageOrientation) {
  MOZ_COUNT_CTOR(nsStylePage);
}

nsChangeHint nsStylePage::CalcDifference(const nsStylePage& aNewData) const {
  // Page rule styling only matters when printing or using print preview.
  if (aNewData.mSize != mSize || aNewData.mPage != mPage ||
      aNewData.mPageOrientation != mPageOrientation) {
    return nsChangeHint_NeutralChange;
  }
  return nsChangeHint_Empty;
}

// --------------------
// nsStylePosition
//
nsStylePosition::nsStylePosition()
    : mObjectPosition(Position::FromPercentage(0.5f)),
      mOffset(StyleRectWithAllSides(StyleInset::Auto())),
      mWidth(StyleSize::Auto()),
      mMinWidth(StyleSize::Auto()),
      mMaxWidth(StyleMaxSize::None()),
      mHeight(StyleSize::Auto()),
      mMinHeight(StyleSize::Auto()),
      mMaxHeight(StyleMaxSize::None()),
      mPositionAnchor(StylePositionAnchorKeyword::Normal()),
      mPositionVisibility(StylePositionVisibility::ANCHORS_VISIBLE),
      mPositionTryFallbacks(StylePositionTryFallbacks()),
      mPositionTryOrder(StylePositionTryOrder::Normal),
      mFlexBasis(StyleFlexBasis::Size(StyleSize::Auto())),
      mAspectRatio(StyleAspectRatio::Auto()),
      mGridAutoFlow(StyleGridAutoFlow::ROW),
      mMasonryAutoFlow(
          {StyleMasonryPlacement::Pack, StyleMasonryItemOrder::DefiniteFirst}),
      mAlignContent({StyleAlignFlags::NORMAL}),
      mAlignItems({StyleAlignFlags::NORMAL}),
      mAlignSelf({StyleAlignFlags::AUTO}),
      mJustifyContent({StyleAlignFlags::NORMAL}),
      mJustifyItems({{StyleAlignFlags::LEGACY}, {StyleAlignFlags::NORMAL}}),
      mJustifySelf({StyleAlignFlags::AUTO}),
      mFlexDirection(StyleFlexDirection::Row),
      mFlexWrap(StyleFlexWrap::Nowrap),
      mObjectFit(StyleObjectFit::Fill),
      mBoxSizing(StyleBoxSizing::ContentBox),
      mOrder(0),
      mFlexGrow(0.0f),
      mFlexShrink(1.0f),
      mZIndex(StyleZIndex::Auto()),
      mGridTemplateColumns(StyleGridTemplateComponent::None()),
      mGridTemplateRows(StyleGridTemplateComponent::None()),
      mGridTemplateAreas(StyleGridTemplateAreas::None()),
      mColumnGap(NonNegativeLengthPercentageOrNormal::Normal()),
      mRowGap(NonNegativeLengthPercentageOrNormal::Normal()),
      mContainIntrinsicWidth(StyleContainIntrinsicSize::None()),
      mContainIntrinsicHeight(StyleContainIntrinsicSize::None()) {
  MOZ_COUNT_CTOR(nsStylePosition);

  // The initial value of grid-auto-columns and grid-auto-rows is 'auto',
  // which computes to 'minmax(auto, auto)'.

  // Other members get their default constructors
  // which initialize them to representations of their respective initial value.
  // mGridTemplate{Rows,Columns}: false and empty arrays for 'none'
  // mGrid{Column,Row}{Start,End}: false/0/empty values for 'auto'
}

nsStylePosition::nsStylePosition(const nsStylePosition& aSource)
    : mObjectPosition(aSource.mObjectPosition),
      mOffset(aSource.mOffset),
      mWidth(aSource.mWidth),
      mMinWidth(aSource.mMinWidth),
      mMaxWidth(aSource.mMaxWidth),
      mHeight(aSource.mHeight),
      mMinHeight(aSource.mMinHeight),
      mMaxHeight(aSource.mMaxHeight),
      mPositionAnchor(aSource.mPositionAnchor),
      mPositionArea(aSource.mPositionArea),
      mPositionVisibility(aSource.mPositionVisibility),
      mPositionTryFallbacks(aSource.mPositionTryFallbacks),
      mPositionTryOrder(aSource.mPositionTryOrder),
      mFlexBasis(aSource.mFlexBasis),
      mGridAutoColumns(aSource.mGridAutoColumns),
      mGridAutoRows(aSource.mGridAutoRows),
      mAspectRatio(aSource.mAspectRatio),
      mGridAutoFlow(aSource.mGridAutoFlow),
      mMasonryAutoFlow(aSource.mMasonryAutoFlow),
      mAlignContent(aSource.mAlignContent),
      mAlignItems(aSource.mAlignItems),
      mAlignSelf(aSource.mAlignSelf),
      mJustifyContent(aSource.mJustifyContent),
      mJustifyItems(aSource.mJustifyItems),
      mJustifySelf(aSource.mJustifySelf),
      mFlexDirection(aSource.mFlexDirection),
      mFlexWrap(aSource.mFlexWrap),
      mObjectFit(aSource.mObjectFit),
      mBoxSizing(aSource.mBoxSizing),
      mOrder(aSource.mOrder),
      mFlexGrow(aSource.mFlexGrow),
      mFlexShrink(aSource.mFlexShrink),
      mZIndex(aSource.mZIndex),
      mGridTemplateColumns(aSource.mGridTemplateColumns),
      mGridTemplateRows(aSource.mGridTemplateRows),
      mGridTemplateAreas(aSource.mGridTemplateAreas),
      mGridColumnStart(aSource.mGridColumnStart),
      mGridColumnEnd(aSource.mGridColumnEnd),
      mGridRowStart(aSource.mGridRowStart),
      mGridRowEnd(aSource.mGridRowEnd),
      mColumnGap(aSource.mColumnGap),
      mRowGap(aSource.mRowGap),
      mContainIntrinsicWidth(aSource.mContainIntrinsicWidth),
      mContainIntrinsicHeight(aSource.mContainIntrinsicHeight) {
  MOZ_COUNT_CTOR(nsStylePosition);
}

static bool IsEqualInsetType(const StyleRect<StyleInset>& aSides1,
                             const StyleRect<StyleInset>& aSides2) {
  for (const auto side : mozilla::AllPhysicalSides()) {
    if (aSides1.Get(side).tag != aSides2.Get(side).tag) {
      return false;
    }
  }
  return true;
}

nsChangeHint nsStylePosition::CalcDifference(
    const nsStylePosition& aNewData, const ComputedStyle& aOldStyle) const {
  if (mGridTemplateColumns.IsMasonry() !=
          aNewData.mGridTemplateColumns.IsMasonry() ||
      mGridTemplateRows.IsMasonry() != aNewData.mGridTemplateRows.IsMasonry()) {
    // XXXmats this could be optimized to AllReflowHints with a bit of work,
    // but I'll assume this is a very rare use case in practice. (bug 1623886)
    return nsChangeHint_ReconstructFrame;
  }

  nsChangeHint hint = nsChangeHint(0);

  // Changes to "z-index" require a repaint.
  if (mZIndex != aNewData.mZIndex) {
    hint |= nsChangeHint_RepaintFrame;
  }

  // Changes to "object-fit" & "object-position" require a repaint.  They
  // may also require a reflow, if we have a nsSubDocumentFrame, so that we
  // can adjust the size & position of the subdocument.
  if (mObjectFit != aNewData.mObjectFit ||
      mObjectPosition != aNewData.mObjectPosition) {
    hint |= nsChangeHint_RepaintFrame | nsChangeHint_NeedReflow;
  }

  if (mContainIntrinsicWidth != aNewData.mContainIntrinsicWidth ||
      mContainIntrinsicHeight != aNewData.mContainIntrinsicHeight) {
    hint |= NS_STYLE_HINT_REFLOW;
  }

  if (mOrder != aNewData.mOrder) {
    // "order" impacts both layout order and stacking order, so we need both a
    // reflow and a repaint when it changes.  (Technically, we only need a
    // reflow if we're in a multi-line flexbox (which we can't be sure about,
    // since that's determined by styling on our parent) -- there, "order" can
    // affect which flex line we end up on, & hence can affect our sizing by
    // changing the group of flex items we're competing with for space.)
    return hint | nsChangeHint_RepaintFrame | nsChangeHint_AllReflowHints;
  }

  if (mBoxSizing != aNewData.mBoxSizing) {
    // Can affect both widths and heights; just a bad scene.
    return hint | nsChangeHint_AllReflowHints;
  }

  if (mAlignItems != aNewData.mAlignItems ||
      mAlignSelf != aNewData.mAlignSelf) {
    return hint | nsChangeHint_AllReflowHints;
  }

  // Properties that apply to flex items:
  // XXXdholbert These should probably be more targeted (bug 819536)
  if (mFlexBasis != aNewData.mFlexBasis || mFlexGrow != aNewData.mFlexGrow ||
      mFlexShrink != aNewData.mFlexShrink) {
    return hint | nsChangeHint_AllReflowHints;
  }

  // Properties that apply to flex containers:
  // - flex-direction can swap a flex container between vertical & horizontal.
  // - flex-wrap changes whether a flex container's children are wrapped, which
  //   impacts their sizing/positioning and hence impacts the container's size.
  if (mFlexDirection != aNewData.mFlexDirection ||
      mFlexWrap != aNewData.mFlexWrap) {
    return hint | nsChangeHint_AllReflowHints;
  }

  // Properties that apply to grid containers:
  // FIXME: only for grid containers
  // (ie. 'display: grid' or 'display: inline-grid')
  if (mGridTemplateColumns != aNewData.mGridTemplateColumns ||
      mGridTemplateRows != aNewData.mGridTemplateRows ||
      mGridTemplateAreas != aNewData.mGridTemplateAreas ||
      mGridAutoColumns != aNewData.mGridAutoColumns ||
      mGridAutoRows != aNewData.mGridAutoRows ||
      mGridAutoFlow != aNewData.mGridAutoFlow ||
      mMasonryAutoFlow != aNewData.mMasonryAutoFlow) {
    return hint | nsChangeHint_AllReflowHints;
  }

  // Properties that apply to grid items:
  // FIXME: only for grid items
  // (ie. parent frame is 'display: grid' or 'display: inline-grid')
  if (mGridColumnStart != aNewData.mGridColumnStart ||
      mGridColumnEnd != aNewData.mGridColumnEnd ||
      mGridRowStart != aNewData.mGridRowStart ||
      mGridRowEnd != aNewData.mGridRowEnd ||
      mColumnGap != aNewData.mColumnGap || mRowGap != aNewData.mRowGap) {
    return hint | nsChangeHint_AllReflowHints;
  }

  // Changing 'justify-content/items/self' might affect the positioning,
  // but it won't affect any sizing.
  if (mJustifyContent != aNewData.mJustifyContent ||
      mJustifyItems.computed != aNewData.mJustifyItems.computed ||
      mJustifySelf != aNewData.mJustifySelf) {
    hint |= nsChangeHint_NeedReflow;
  }

  // No need to do anything if specified justify-items changes, as long as the
  // computed one (tested above) is unchanged.
  if (mJustifyItems.specified != aNewData.mJustifyItems.specified) {
    hint |= nsChangeHint_NeutralChange;
  }

  // 'align-content' doesn't apply to a single-line flexbox but we don't know
  // if we're a flex container at this point so we can't optimize for that.
  if (mAlignContent != aNewData.mAlignContent) {
    hint |= nsChangeHint_NeedReflow;
  }

  bool widthChanged = mWidth != aNewData.mWidth ||
                      mMinWidth != aNewData.mMinWidth ||
                      mMaxWidth != aNewData.mMaxWidth;
  bool heightChanged = mHeight != aNewData.mHeight ||
                       mMinHeight != aNewData.mMinHeight ||
                       mMaxHeight != aNewData.mMaxHeight;

  if (widthChanged || heightChanged) {
    // It doesn't matter whether we're looking at the old or new visibility
    // struct, since a change between vertical and horizontal writing-mode will
    // cause a reframe.
    const bool isVertical = aOldStyle.StyleVisibility()->mWritingMode !=
                            StyleWritingModeProperty::HorizontalTb;
    if (isVertical ? widthChanged : heightChanged) {
      hint |= nsChangeHint_ReflowHintsForBSizeChange;
    }
    if (isVertical ? heightChanged : widthChanged) {
      hint |= nsChangeHint_ReflowHintsForISizeChange;
    }
  }

  // Note(dshin): Following hints based on changes in `position-*`
  // is conditional on being absolutely positioned, but we don't have
  // enough information here.
  if (mPositionVisibility != aNewData.mPositionVisibility) {
    // position-visibility doesn't affect layout boxes.
    hint |= nsChangeHint_RepaintFrame;
  }

  if (mPositionAnchor != aNewData.mPositionAnchor ||
      mPositionTryFallbacks != aNewData.mPositionTryFallbacks ||
      mPositionTryOrder != aNewData.mPositionTryOrder ||
      mPositionArea != aNewData.mPositionArea) {
    // We need to reflow in order to update the `AnchorPosReferences`
    // property at minimum.
    hint |= nsChangeHint_NeedReflow | nsChangeHint_ReflowChangesSizeOrPosition;
  }

  if (mAspectRatio != aNewData.mAspectRatio) {
    hint |= nsChangeHint_ReflowHintsForISizeChange |
            nsChangeHint_ReflowHintsForBSizeChange;
  }

  // If any of the offsets have changed, then return the respective hints
  // so that we would hopefully be able to avoid reflowing.
  // Note that it is possible that we'll need to reflow when processing
  // restyles, but we don't have enough information to make a good decision
  // right now.
  // Don't try to handle changes between types efficiently; at least for
  // changing into/out of `auto`, we will hardly ever be able to avoid a reflow.
  if (mOffset != aNewData.mOffset) {
    if (IsEqualInsetType(mOffset, aNewData.mOffset) &&
        aNewData.mOffset.All([](const StyleInset& aInset) {
          // Anchor positioning invalidation depends on `AnchorPosReferences`
          // being updated, which happens during reflow.
          return !aInset.HasAnchorPositioningFunction();
        })) {
      hint |=
          nsChangeHint_RecomputePosition | nsChangeHint_UpdateParentOverflow;
    } else {
      hint |=
          nsChangeHint_NeedReflow | nsChangeHint_ReflowChangesSizeOrPosition;
    }
  }
  return hint;
}

const StyleContainIntrinsicSize& nsStylePosition::ContainIntrinsicBSize(
    const WritingMode& aWM) const {
  return aWM.IsVertical() ? mContainIntrinsicWidth : mContainIntrinsicHeight;
}

const StyleContainIntrinsicSize& nsStylePosition::ContainIntrinsicISize(
    const WritingMode& aWM) const {
  return aWM.IsVertical() ? mContainIntrinsicHeight : mContainIntrinsicWidth;
}

StyleSelfAlignment nsStylePosition::UsedAlignSelf(
    const ComputedStyle* aParent) const {
  if (mAlignSelf._0 != StyleAlignFlags::AUTO) {
    return mAlignSelf;
  }
  if (MOZ_LIKELY(aParent)) {
    auto parentAlignItems = aParent->StylePosition()->mAlignItems;
    MOZ_ASSERT(!(parentAlignItems._0 & StyleAlignFlags::LEGACY),
               "align-items can't have 'legacy'");
    return {parentAlignItems._0};
  }
  return {StyleAlignFlags::NORMAL};
}

StyleSelfAlignment nsStylePosition::UsedJustifySelf(
    const ComputedStyle* aParent) const {
  if (mJustifySelf._0 != StyleAlignFlags::AUTO) {
    return mJustifySelf;
  }
  if (MOZ_LIKELY(aParent)) {
    const auto& inheritedJustifyItems =
        aParent->StylePosition()->mJustifyItems.computed._0;
    return {inheritedJustifyItems._0 & ~StyleAlignFlags::LEGACY};
  }
  return {StyleAlignFlags::NORMAL};
}

AnchorResolvedInset AnchorResolvedInsetHelper::ResolveAnchor(
    const mozilla::StyleInset& aValue, mozilla::StylePhysicalSide aSide,
    const AnchorPosOffsetResolutionParams& aParams) {
  MOZ_ASSERT(aValue.HasAnchorPositioningFunction(),
             "Calling anchor resolution without using it?");
  switch (aValue.tag) {
    case StyleInset::Tag::AnchorContainingCalcFunction: {
      const auto& lp = aValue.AsAnchorContainingCalcFunction();
      const auto& c = lp.AsCalc();
      auto result = StyleCalcAnchorPositioningFunctionResolution::Invalid();
      const auto allowed =
          StyleAllowAnchorPosResolutionInCalcPercentage::Both(aSide);
      Servo_ResolveAnchorFunctionsInCalcPercentage(&c, &allowed, &aParams,
                                                   &result);
      if (result.IsInvalid()) {
        return Auto();
      }
      return MakeUniqueOfUniqueOrNonOwning<const StyleInset>(result.AsValid());
    }
    case StyleInset::Tag::AnchorFunction: {
      auto resolved = StyleAnchorPositioningFunctionResolution::Invalid();
      Servo_ResolveAnchorFunction(&*aValue.AsAnchorFunction(), &aParams, aSide,
                                  &resolved);
      if (resolved.IsInvalid()) {
        return Auto();
      }
      if (resolved.IsResolvedReference()) {
        return MakeUniqueOfUniqueOrNonOwning<const StyleInset>(
            *resolved.AsResolvedReference());
      }
      return AnchorResolvedInset{
          MakeUniqueOfUniqueOrNonOwning<const StyleInset>(
              resolved.AsResolved())};
    }
    case StyleInset::Tag::AnchorSizeFunction: {
      auto resolved = StyleAnchorPositioningFunctionResolution::Invalid();
      Servo_ResolveAnchorSizeFunctionForInset(
          &*aValue.AsAnchorSizeFunction(), &aParams, ToStylePhysicalAxis(aSide),
          &resolved);
      if (resolved.IsInvalid()) {
        return Auto();
      }
      if (resolved.IsResolvedReference()) {
        return MakeUniqueOfUniqueOrNonOwning<const StyleInset>(
            *resolved.AsResolvedReference());
      }
      return MakeUniqueOfUniqueOrNonOwning<const StyleInset>(
          resolved.AsResolved());
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled inset type");
      return Auto();
  }
}

AnchorResolvedSize AnchorResolvedSizeHelper::ResolveAnchor(
    const mozilla::StyleSize& aValue, StylePhysicalAxis aAxis,
    const AnchorPosResolutionParams& aParams) {
  MOZ_ASSERT(aValue.HasAnchorPositioningFunction(),
             "Calling anchor resolution without using it?");
  if (aValue.IsAnchorSizeFunction()) {
    auto resolved = StyleAnchorPositioningFunctionResolution::Invalid();
    Servo_ResolveAnchorSizeFunctionForSize(&*aValue.AsAnchorSizeFunction(),
                                           &aParams, aAxis, &resolved);
    if (resolved.IsInvalid()) {
      return Auto();
    }
    if (resolved.IsResolvedReference()) {
      return MakeUniqueOfUniqueOrNonOwning<const StyleSize>(
          *resolved.AsResolvedReference());
    }
    return MakeUniqueOfUniqueOrNonOwning<const StyleSize>(
        resolved.AsResolved());
  }

  const auto& lp = aValue.AsAnchorContainingCalcFunction();
  // Follows the same reasoning as anchor resolved insets.
  const auto& c = lp.AsCalc();
  auto result = StyleCalcAnchorPositioningFunctionResolution::Invalid();
  AnchorPosOffsetResolutionParams params =
      AnchorPosOffsetResolutionParams::UseCBFrameSize(aParams);
  const auto allowed =
      StyleAllowAnchorPosResolutionInCalcPercentage::AnchorSizeOnly(aAxis);
  Servo_ResolveAnchorFunctionsInCalcPercentage(&c, &allowed, &params, &result);
  if (result.IsInvalid()) {
    return Auto();
  }
  return MakeUniqueOfUniqueOrNonOwning<const StyleSize>(result.AsValid());
}

AnchorResolvedMaxSize AnchorResolvedMaxSizeHelper::ResolveAnchor(
    const mozilla::StyleMaxSize& aValue, StylePhysicalAxis aAxis,
    const AnchorPosResolutionParams& aParams) {
  MOZ_ASSERT(aValue.HasAnchorPositioningFunction(),
             "Calling anchor resolution without using it?");
  if (aValue.IsAnchorSizeFunction()) {
    auto resolved = StyleAnchorPositioningFunctionResolution::Invalid();
    Servo_ResolveAnchorSizeFunctionForMaxSize(&*aValue.AsAnchorSizeFunction(),
                                              &aParams, aAxis, &resolved);
    if (resolved.IsInvalid()) {
      return None();
    }
    if (resolved.IsResolvedReference()) {
      return MakeUniqueOfUniqueOrNonOwning<const StyleMaxSize>(
          *resolved.AsResolvedReference());
    }
    return MakeUniqueOfUniqueOrNonOwning<const StyleMaxSize>(
        resolved.AsResolved());
  }

  const auto& lp = aValue.AsAnchorContainingCalcFunction();
  // Follows the same reasoning as anchor resolved insets.
  const auto& c = lp.AsCalc();
  auto result = StyleCalcAnchorPositioningFunctionResolution::Invalid();
  AnchorPosOffsetResolutionParams params =
      AnchorPosOffsetResolutionParams::UseCBFrameSize(aParams);
  const auto allowed =
      StyleAllowAnchorPosResolutionInCalcPercentage::AnchorSizeOnly(aAxis);
  Servo_ResolveAnchorFunctionsInCalcPercentage(&c, &allowed, &params, &result);
  if (result.IsInvalid()) {
    return None();
  }
  return MakeUniqueOfUniqueOrNonOwning<const StyleMaxSize>(result.AsValid());
}

// --------------------
// nsStyleTable
//

nsStyleTable::nsStyleTable()
    : mLayoutStrategy(StyleTableLayout::Auto), mXSpan(1) {
  MOZ_COUNT_CTOR(nsStyleTable);
}

nsStyleTable::nsStyleTable(const nsStyleTable& aSource)
    : mLayoutStrategy(aSource.mLayoutStrategy), mXSpan(aSource.mXSpan) {
  MOZ_COUNT_CTOR(nsStyleTable);
}

nsChangeHint nsStyleTable::CalcDifference(const nsStyleTable& aNewData) const {
  if (mXSpan != aNewData.mXSpan ||
      mLayoutStrategy != aNewData.mLayoutStrategy) {
    return nsChangeHint_ReconstructFrame;
  }
  return nsChangeHint(0);
}

// -----------------------
// nsStyleTableBorder

nsStyleTableBorder::nsStyleTableBorder()
    : mBorderSpacing{Length::Zero(), Length::Zero()},
      mBorderCollapse(StyleBorderCollapse::Separate),
      mCaptionSide(StyleCaptionSide::Top),
      mEmptyCells(StyleEmptyCells::Show) {
  MOZ_COUNT_CTOR(nsStyleTableBorder);
}

nsStyleTableBorder::nsStyleTableBorder(const nsStyleTableBorder& aSource)
    : mBorderSpacing(aSource.mBorderSpacing),
      mBorderCollapse(aSource.mBorderCollapse),
      mCaptionSide(aSource.mCaptionSide),
      mEmptyCells(aSource.mEmptyCells) {
  MOZ_COUNT_CTOR(nsStyleTableBorder);
}

nsChangeHint nsStyleTableBorder::CalcDifference(
    const nsStyleTableBorder& aNewData) const {
  // Border-collapse changes need a reframe, because we use a different frame
  // class for table cells in the collapsed border model.  This is used to
  // conserve memory when using the separated border model (collapsed borders
  // require extra state to be stored).
  if (mBorderCollapse != aNewData.mBorderCollapse) {
    return nsChangeHint_ReconstructFrame;
  }
  if (mCaptionSide != aNewData.mCaptionSide ||
      mBorderSpacing != aNewData.mBorderSpacing) {
    return NS_STYLE_HINT_REFLOW;
  }
  if (mEmptyCells != aNewData.mEmptyCells) {
    return NS_STYLE_HINT_VISUAL;
  }
  return nsChangeHint(0);
}

template <typename T>
static bool GradientItemsAreOpaque(
    Span<const StyleGenericGradientItem<StyleColor, T>> aItems) {
  for (auto& stop : aItems) {
    if (stop.IsInterpolationHint()) {
      continue;
    }

    auto& color = stop.IsSimpleColorStop() ? stop.AsSimpleColorStop()
                                           : stop.AsComplexColorStop().color;
    if (color.MaybeTransparent()) {
      // We don't know the foreground color here, so if it's being used
      // we must assume it might be transparent.
      return false;
    }
  }

  return true;
}

template <>
bool StyleGradient::IsOpaque() const {
  switch (tag) {
    case Tag::Linear:
      return GradientItemsAreOpaque(AsLinear().items.AsSpan());
    case Tag::Radial:
      return GradientItemsAreOpaque(AsRadial().items.AsSpan());
    case Tag::Conic:
      return GradientItemsAreOpaque(AsConic().items.AsSpan());
  }
  MOZ_ASSERT_UNREACHABLE("Unexpected gradient type");
  return false;
}

template <>
bool StyleImage::IsOpaque() const {
  switch (tag) {
    case Tag::ImageSet:
      return FinalImage().IsOpaque();
    case Tag::Gradient:
      return AsGradient()->IsOpaque();
    case Tag::Url: {
      if (!IsComplete()) {
        return false;
      }
      MOZ_ASSERT(GetImageRequest(), "should've returned earlier above");
      nsCOMPtr<imgIContainer> imageContainer;
      GetImageRequest()->GetImage(getter_AddRefs(imageContainer));
      MOZ_ASSERT(imageContainer, "IsComplete() said image container is ready");
      return imageContainer->WillDrawOpaqueNow();
    }
    case Tag::Image:
      return !AsImage()->MaybeTransparent();
    case Tag::CrossFade:
      for (const auto& el : AsCrossFade()->elements.AsSpan()) {
        if (el.image.IsColor()) {
          if (el.image.AsColor().MaybeTransparent()) {
            return false;
          }
          continue;
        }
        MOZ_ASSERT(el.image.IsImage());
        if (!el.image.AsImage().IsOpaque()) {
          return false;
        }
      }
      return true;
    case Tag::LightDark:
      MOZ_FALLTHROUGH_ASSERT("Should be computed already");
    case Tag::Element:
    case Tag::MozSymbolicIcon:
    case Tag::None:
      break;
  }
  return false;
}

template <>
bool StyleImage::IsComplete() const {
  switch (tag) {
    case Tag::None:
      return false;
    case Tag::Gradient:
    case Tag::Element:
    case Tag::MozSymbolicIcon:
    case Tag::Image:
      return true;
    case Tag::Url: {
      if (!IsResolved()) {
        return false;
      }
      imgRequestProxy* req = GetImageRequest();
      if (!req) {
        return false;
      }
      uint32_t status = imgIRequest::STATUS_ERROR;
      return NS_SUCCEEDED(req->GetImageStatus(&status)) &&
             (status & imgIRequest::STATUS_SIZE_AVAILABLE) &&
             (status & imgIRequest::STATUS_FRAME_COMPLETE);
    }
    case Tag::ImageSet:
      return FinalImage().IsComplete();
    // Bug 546052 cross-fade not yet implemented.
    case Tag::CrossFade:
      return true;
    case Tag::LightDark:
      MOZ_ASSERT_UNREACHABLE("light-dark() should be computed already");
      break;
  }
  MOZ_ASSERT_UNREACHABLE("unexpected image type");
  return false;
}

template <>
bool StyleImage::IsSizeAvailable() const {
  switch (tag) {
    case Tag::None:
      return false;
    case Tag::Image:
    case Tag::Gradient:
    case Tag::Element:
    case Tag::MozSymbolicIcon:
      return true;
    case Tag::Url: {
      imgRequestProxy* req = GetImageRequest();
      if (!req) {
        return false;
      }
      uint32_t status = imgIRequest::STATUS_ERROR;
      return NS_SUCCEEDED(req->GetImageStatus(&status)) &&
             !(status & imgIRequest::STATUS_ERROR) &&
             (status & imgIRequest::STATUS_SIZE_AVAILABLE);
    }
    case Tag::ImageSet:
      return FinalImage().IsSizeAvailable();
    case Tag::CrossFade:
      // TODO: Bug 546052 cross-fade not yet implemented.
      return true;
    case Tag::LightDark:
      MOZ_ASSERT_UNREACHABLE("light-dark() should be computed already");
      break;
  }
  MOZ_ASSERT_UNREACHABLE("unexpected image type");
  return false;
}

template <>
void StyleImage::ResolveImage(Document& aDoc, const StyleImage* aOld) {
  if (IsResolved()) {
    return;
  }
  const auto* old = aOld ? aOld->GetImageRequestURLValue() : nullptr;
  const auto* url = GetImageRequestURLValue();
  // We could avoid this const_cast generating more code but it's not really
  // worth it.
  const_cast<StyleComputedUrl*>(url)->ResolveImage(aDoc, old);
}

template <>
ImageResolution StyleImage::GetResolution(
    const ComputedStyle* aStyleForZoom) const {
  ImageResolution resolution;
  if (imgRequestProxy* request = GetImageRequest()) {
    RefPtr<imgIContainer> image;
    request->GetImage(getter_AddRefs(image));
    if (image) {
      resolution = image->GetResolution();
    }
  }
  if (IsImageSet()) {
    const auto& set = *AsImageSet();
    auto items = set.items.AsSpan();
    if (MOZ_LIKELY(set.selected_index < items.Length())) {
      float r = items[set.selected_index].resolution._0;
      resolution.ScaleBy(r);
    }
  }
  if (aStyleForZoom && aStyleForZoom->EffectiveZoom() != StyleZoom::ONE) {
    resolution.ScaleBy(1.0f / aStyleForZoom->EffectiveZoom().ToFloat());
  }
  return resolution;
}

// --------------------
// nsStyleImageLayers
//

const NonCustomCSSPropertyId nsStyleImageLayers::kBackgroundLayerTable[] = {
    eCSSProperty_background,             // shorthand
    eCSSProperty_background_color,       // color
    eCSSProperty_background_image,       // image
    eCSSProperty_background_repeat,      // repeat
    eCSSProperty_background_position_x,  // positionX
    eCSSProperty_background_position_y,  // positionY
    eCSSProperty_background_clip,        // clip
    eCSSProperty_background_origin,      // origin
    eCSSProperty_background_size,        // size
    eCSSProperty_background_attachment,  // attachment
    eCSSProperty_UNKNOWN,                // maskMode
    eCSSProperty_UNKNOWN                 // composite
};

const NonCustomCSSPropertyId nsStyleImageLayers::kMaskLayerTable[] = {
    eCSSProperty_mask,             // shorthand
    eCSSProperty_UNKNOWN,          // color
    eCSSProperty_mask_image,       // image
    eCSSProperty_mask_repeat,      // repeat
    eCSSProperty_mask_position_x,  // positionX
    eCSSProperty_mask_position_y,  // positionY
    eCSSProperty_mask_clip,        // clip
    eCSSProperty_mask_origin,      // origin
    eCSSProperty_mask_size,        // size
    eCSSProperty_UNKNOWN,          // attachment
    eCSSProperty_mask_mode,        // maskMode
    eCSSProperty_mask_composite    // composite
};

nsStyleImageLayers::nsStyleImageLayers(nsStyleImageLayers::LayerType aType)
    : mAttachmentCount(1),
      mClipCount(1),
      mOriginCount(1),
      mRepeatCount(1),
      mPositionXCount(1),
      mPositionYCount(1),
      mImageCount(1),
      mSizeCount(1),
      mMaskModeCount(1),
      mBlendModeCount(1),
      mCompositeCount(1),
      mLayers(nsStyleAutoArray<Layer>::WITH_SINGLE_INITIAL_ELEMENT) {
  // Ensure first layer is initialized as specified layer type
  mLayers[0].Initialize(aType);
}

nsStyleImageLayers::nsStyleImageLayers(const nsStyleImageLayers& aSource)
    : mAttachmentCount(aSource.mAttachmentCount),
      mClipCount(aSource.mClipCount),
      mOriginCount(aSource.mOriginCount),
      mRepeatCount(aSource.mRepeatCount),
      mPositionXCount(aSource.mPositionXCount),
      mPositionYCount(aSource.mPositionYCount),
      mImageCount(aSource.mImageCount),
      mSizeCount(aSource.mSizeCount),
      mMaskModeCount(aSource.mMaskModeCount),
      mBlendModeCount(aSource.mBlendModeCount),
      mCompositeCount(aSource.mCompositeCount),
      mLayers(aSource.mLayers.Clone()) {}

static bool AnyLayerIsElementImage(const nsStyleImageLayers& aLayers) {
  NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(i, aLayers) {
    if (aLayers.mLayers[i].mImage.FinalImage().IsElement()) {
      return true;
    }
  }
  return false;
}

nsChangeHint nsStyleImageLayers::CalcDifference(
    const nsStyleImageLayers& aNewLayers, LayerType aType) const {
  nsChangeHint hint = nsChangeHint(0);

  // If the number of visible images changes, then it's easy-peasy.
  if (mImageCount != aNewLayers.mImageCount) {
    hint |= nsChangeHint_RepaintFrame;
    if (aType == nsStyleImageLayers::LayerType::Mask ||
        AnyLayerIsElementImage(*this) || AnyLayerIsElementImage(aNewLayers)) {
      hint |= nsChangeHint_UpdateEffects;
    }
    return hint;
  }

  const nsStyleImageLayers& moreLayers =
      mLayers.Length() > aNewLayers.mLayers.Length() ? *this : aNewLayers;
  const nsStyleImageLayers& lessLayers =
      mLayers.Length() > aNewLayers.mLayers.Length() ? aNewLayers : *this;

  for (size_t i = 0; i < moreLayers.mLayers.Length(); ++i) {
    const Layer& moreLayersLayer = moreLayers.mLayers[i];
    if (i < moreLayers.mImageCount) {
      // This is a visible image we're diffing, we may need to repaint.
      const Layer& lessLayersLayer = lessLayers.mLayers[i];
      nsChangeHint layerDifference =
          moreLayersLayer.CalcDifference(lessLayersLayer);
      if (layerDifference &&
          (moreLayersLayer.mImage.FinalImage().IsElement() ||
           lessLayersLayer.mImage.FinalImage().IsElement())) {
        layerDifference |=
            nsChangeHint_UpdateEffects | nsChangeHint_RepaintFrame;
      }
      hint |= layerDifference;
      continue;
    }
    if (hint) {
      // If they're different by now, we're done.
      return hint;
    }
    if (i >= lessLayers.mLayers.Length()) {
      // The layer count differs, we know some property has changed, but if we
      // got here we know it won't affect rendering.
      return nsChangeHint_NeutralChange;
    }

    const Layer& lessLayersLayer = lessLayers.mLayers[i];
    MOZ_ASSERT(moreLayersLayer.mImage.IsNone());
    MOZ_ASSERT(lessLayersLayer.mImage.IsNone());
    if (moreLayersLayer.CalcDifference(lessLayersLayer)) {
      // We don't care about the difference returned, we know it's not visible,
      // but if something changed, then we need to return the neutral change.
      return nsChangeHint_NeutralChange;
    }
  }

  if (hint) {
    // If they're different by now, we're done.
    return hint;
  }

  // We could have same layers and values, but different count still.
  if (mAttachmentCount != aNewLayers.mAttachmentCount ||
      mBlendModeCount != aNewLayers.mBlendModeCount ||
      mClipCount != aNewLayers.mClipCount ||
      mCompositeCount != aNewLayers.mCompositeCount ||
      mMaskModeCount != aNewLayers.mMaskModeCount ||
      mOriginCount != aNewLayers.mOriginCount ||
      mRepeatCount != aNewLayers.mRepeatCount ||
      mPositionXCount != aNewLayers.mPositionXCount ||
      mPositionYCount != aNewLayers.mPositionYCount ||
      mSizeCount != aNewLayers.mSizeCount) {
    hint |= nsChangeHint_NeutralChange;
  }

  return hint;
}

nsStyleImageLayers& nsStyleImageLayers::operator=(
    const nsStyleImageLayers& aOther) {
  mAttachmentCount = aOther.mAttachmentCount;
  mClipCount = aOther.mClipCount;
  mOriginCount = aOther.mOriginCount;
  mRepeatCount = aOther.mRepeatCount;
  mPositionXCount = aOther.mPositionXCount;
  mPositionYCount = aOther.mPositionYCount;
  mImageCount = aOther.mImageCount;
  mSizeCount = aOther.mSizeCount;
  mMaskModeCount = aOther.mMaskModeCount;
  mBlendModeCount = aOther.mBlendModeCount;
  mCompositeCount = aOther.mCompositeCount;
  mLayers = aOther.mLayers.Clone();

  return *this;
}

bool nsStyleImageLayers::operator==(const nsStyleImageLayers& aOther) const {
  if (mAttachmentCount != aOther.mAttachmentCount ||
      mClipCount != aOther.mClipCount || mOriginCount != aOther.mOriginCount ||
      mRepeatCount != aOther.mRepeatCount ||
      mPositionXCount != aOther.mPositionXCount ||
      mPositionYCount != aOther.mPositionYCount ||
      mImageCount != aOther.mImageCount || mSizeCount != aOther.mSizeCount ||
      mMaskModeCount != aOther.mMaskModeCount ||
      mBlendModeCount != aOther.mBlendModeCount) {
    return false;
  }

  if (mLayers.Length() != aOther.mLayers.Length()) {
    return false;
  }

  for (uint32_t i = 0; i < mLayers.Length(); i++) {
    if (mLayers[i].mPosition != aOther.mLayers[i].mPosition ||
        mLayers[i].mImage != aOther.mLayers[i].mImage ||
        mLayers[i].mSize != aOther.mLayers[i].mSize ||
        mLayers[i].mClip != aOther.mLayers[i].mClip ||
        mLayers[i].mOrigin != aOther.mLayers[i].mOrigin ||
        mLayers[i].mAttachment != aOther.mLayers[i].mAttachment ||
        mLayers[i].mBlendMode != aOther.mLayers[i].mBlendMode ||
        mLayers[i].mComposite != aOther.mLayers[i].mComposite ||
        mLayers[i].mMaskMode != aOther.mLayers[i].mMaskMode ||
        mLayers[i].mRepeat != aOther.mLayers[i].mRepeat) {
      return false;
    }
  }

  return true;
}

static bool SizeDependsOnPositioningAreaSize(const StyleBackgroundSize& aSize,
                                             const StyleImage& aImage) {
  MOZ_ASSERT(!aImage.IsNone(), "caller should have handled this");
  MOZ_ASSERT(!aImage.IsImageSet(), "caller should have handled this");

  // Contain and cover straightforwardly depend on frame size.
  if (aSize.IsCover() || aSize.IsContain()) {
    return true;
  }

  MOZ_ASSERT(aSize.IsExplicitSize());
  const auto& size = aSize.AsExplicitSize();

  // If either dimension contains a non-zero percentage, rendering for that
  // dimension straightforwardly depends on frame size.
  if (size.width.HasPercent() || size.height.HasPercent()) {
    return true;
  }

  // If both dimensions are fixed lengths, there's no dependency.
  if (!size.width.IsAuto() && !size.height.IsAuto()) {
    return false;
  }

  // Gradient and image() rendering depends on frame size when auto is involved
  // because they have no intrinsic ratio or dimensions, and therefore the
  // relevant dimension is "treat[ed] as 100%".
  if (aImage.IsGradient() || aImage.IsImage()) {
    return true;
  }

  // XXX Element rendering for auto or fixed length doesn't depend on frame size
  //     according to the spec.  However, we don't implement the spec yet, so
  //     for now we bail and say element() plus auto affects ultimate size.
  if (aImage.IsElement()) {
    return true;
  }

  MOZ_ASSERT(aImage.IsImageRequestType(), "Missed some image");
  if (auto* request = aImage.GetImageRequest()) {
    nsCOMPtr<imgIContainer> imgContainer;
    request->GetImage(getter_AddRefs(imgContainer));
    if (imgContainer) {
      CSSIntSize imageSize;
      AspectRatio imageRatio;
      bool hasWidth, hasHeight;
      // We could bother getting the right resolution here but it doesn't matter
      // since we ignore `imageSize`.
      nsLayoutUtils::ComputeSizeForDrawing(imgContainer, ImageResolution(),
                                           imageSize, imageRatio, hasWidth,
                                           hasHeight);

      // If the image has a fixed width and height, rendering never depends on
      // the frame size.
      if (hasWidth && hasHeight) {
        return false;
      }

      // If the image has an intrinsic ratio, rendering will depend on frame
      // size when background-size is all auto.
      if (imageRatio) {
        return size.width.IsAuto() == size.height.IsAuto();
      }

      // Otherwise, rendering depends on frame size when the image dimensions
      // and background-size don't complement each other.
      return !(hasWidth && size.width.IsLengthPercentage()) &&
             !(hasHeight && size.height.IsLengthPercentage());
    }
  }

  // Passed the gauntlet: no dependency.
  return false;
}

nsStyleImageLayers::Layer::Layer()
    : mImage(StyleImage::None()),
      mSize(StyleBackgroundSize::ExplicitSize(LengthPercentageOrAuto::Auto(),
                                              LengthPercentageOrAuto::Auto())),

      mClip(StyleGeometryBox::BorderBox),
      mAttachment(StyleImageLayerAttachment::Scroll),
      mBlendMode(StyleBlend::Normal),
      mComposite(StyleMaskComposite::Add),
      mMaskMode(StyleMaskMode::MatchSource) {}

nsStyleImageLayers::Layer::~Layer() = default;

void nsStyleImageLayers::Layer::Initialize(
    nsStyleImageLayers::LayerType aType) {
  mPosition = Position::FromPercentage(0.);

  if (aType == LayerType::Background) {
    mOrigin = StyleGeometryBox::PaddingBox;
  } else {
    MOZ_ASSERT(aType == LayerType::Mask, "unsupported layer type.");
    mOrigin = StyleGeometryBox::BorderBox;
  }
}

bool nsStyleImageLayers::Layer::
    RenderingMightDependOnPositioningAreaSizeChange() const {
  // Do we even have an image?
  if (mImage.IsNone()) {
    return false;
  }

  return mPosition.DependsOnPositioningAreaSize() ||
         SizeDependsOnPositioningAreaSize(mSize, mImage.FinalImage()) ||
         mRepeat.DependsOnPositioningAreaSize();
}

bool nsStyleImageLayers::Layer::operator==(const Layer& aOther) const {
  return mAttachment == aOther.mAttachment && mClip == aOther.mClip &&
         mOrigin == aOther.mOrigin && mRepeat == aOther.mRepeat &&
         mBlendMode == aOther.mBlendMode && mPosition == aOther.mPosition &&
         mSize == aOther.mSize && mImage == aOther.mImage &&
         mMaskMode == aOther.mMaskMode && mComposite == aOther.mComposite;
}

template <class ComputedValueItem>
static void FillImageLayerList(
    nsStyleAutoArray<nsStyleImageLayers::Layer>& aLayers,
    ComputedValueItem nsStyleImageLayers::Layer::* aResultLocation,
    uint32_t aItemCount, uint32_t aFillCount) {
  MOZ_ASSERT(aFillCount <= aLayers.Length(), "unexpected array length");
  for (uint32_t sourceLayer = 0, destLayer = aItemCount; destLayer < aFillCount;
       ++sourceLayer, ++destLayer) {
    aLayers[destLayer].*aResultLocation = aLayers[sourceLayer].*aResultLocation;
  }
}

// The same as FillImageLayerList, but for values stored in
// layer.mPosition.*aResultLocation instead of layer.*aResultLocation.
static void FillImageLayerPositionCoordList(
    nsStyleAutoArray<nsStyleImageLayers::Layer>& aLayers,
    LengthPercentage Position::* aResultLocation, uint32_t aItemCount,
    uint32_t aFillCount) {
  MOZ_ASSERT(aFillCount <= aLayers.Length(), "unexpected array length");
  for (uint32_t sourceLayer = 0, destLayer = aItemCount; destLayer < aFillCount;
       ++sourceLayer, ++destLayer) {
    aLayers[destLayer].mPosition.*aResultLocation =
        aLayers[sourceLayer].mPosition.*aResultLocation;
  }
}

void nsStyleImageLayers::FillAllLayers(uint32_t aMaxItemCount) {
  // Delete any extra items.  We need to keep layers in which any
  // property was specified.
  mLayers.TruncateLengthNonZero(aMaxItemCount);

  uint32_t fillCount = mImageCount;
  FillImageLayerList(mLayers, &Layer::mImage, mImageCount, fillCount);
  FillImageLayerList(mLayers, &Layer::mRepeat, mRepeatCount, fillCount);
  FillImageLayerList(mLayers, &Layer::mAttachment, mAttachmentCount, fillCount);
  FillImageLayerList(mLayers, &Layer::mClip, mClipCount, fillCount);
  FillImageLayerList(mLayers, &Layer::mBlendMode, mBlendModeCount, fillCount);
  FillImageLayerList(mLayers, &Layer::mOrigin, mOriginCount, fillCount);
  FillImageLayerPositionCoordList(mLayers, &Position::horizontal,
                                  mPositionXCount, fillCount);
  FillImageLayerPositionCoordList(mLayers, &Position::vertical, mPositionYCount,
                                  fillCount);
  FillImageLayerList(mLayers, &Layer::mSize, mSizeCount, fillCount);
  FillImageLayerList(mLayers, &Layer::mMaskMode, mMaskModeCount, fillCount);
  FillImageLayerList(mLayers, &Layer::mComposite, mCompositeCount, fillCount);
}

static bool UrlValuesEqual(const StyleImage& aImage,
                           const StyleImage& aOtherImage) {
  const auto* url = aImage.GetImageRequestURLValue();
  const auto* other = aOtherImage.GetImageRequestURLValue();
  return url == other || (url && other && *url == *other);
}

nsChangeHint nsStyleImageLayers::Layer::CalcDifference(
    const nsStyleImageLayers::Layer& aNewLayer) const {
  nsChangeHint hint = nsChangeHint(0);
  if (!UrlValuesEqual(mImage, aNewLayer.mImage)) {
    hint |= nsChangeHint_RepaintFrame | nsChangeHint_UpdateEffects;
  } else if (mAttachment != aNewLayer.mAttachment || mClip != aNewLayer.mClip ||
             mOrigin != aNewLayer.mOrigin || mRepeat != aNewLayer.mRepeat ||
             mBlendMode != aNewLayer.mBlendMode || mSize != aNewLayer.mSize ||
             mImage != aNewLayer.mImage || mMaskMode != aNewLayer.mMaskMode ||
             mComposite != aNewLayer.mComposite) {
    hint |= nsChangeHint_RepaintFrame;
  }

  if (mPosition != aNewLayer.mPosition) {
    hint |= nsChangeHint_UpdateBackgroundPosition;
  }

  return hint;
}

// --------------------
// nsStyleBackground
//

nsStyleBackground::nsStyleBackground()
    : mImage(nsStyleImageLayers::LayerType::Background),
      mBackgroundColor(StyleColor::Transparent()) {
  MOZ_COUNT_CTOR(nsStyleBackground);
}

nsStyleBackground::nsStyleBackground(const nsStyleBackground& aSource)
    : mImage(aSource.mImage), mBackgroundColor(aSource.mBackgroundColor) {
  MOZ_COUNT_CTOR(nsStyleBackground);
}

void nsStyleBackground::TriggerImageLoads(Document& aDocument,
                                          const nsStyleBackground* aOldStyle) {
  MOZ_ASSERT(NS_IsMainThread());
  mImage.ResolveImages(aDocument, aOldStyle ? &aOldStyle->mImage : nullptr);
}

nsChangeHint nsStyleBackground::CalcDifference(
    const nsStyleBackground& aNewData) const {
  nsChangeHint hint = nsChangeHint(0);
  if (mBackgroundColor != aNewData.mBackgroundColor) {
    hint |= nsChangeHint_RepaintFrame;
  }

  hint |= mImage.CalcDifference(aNewData.mImage,
                                nsStyleImageLayers::LayerType::Background);

  return hint;
}

bool nsStyleBackground::HasFixedBackground(nsIFrame* aFrame) const {
  NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(i, mImage) {
    const nsStyleImageLayers::Layer& layer = mImage.mLayers[i];
    if (layer.mAttachment == StyleImageLayerAttachment::Fixed &&
        !layer.mImage.IsNone() && !nsLayoutUtils::IsTransformed(aFrame)) {
      return true;
    }
  }
  return false;
}

nscolor nsStyleBackground::BackgroundColor(const nsIFrame* aFrame) const {
  return mBackgroundColor.CalcColor(aFrame);
}

nscolor nsStyleBackground::BackgroundColor(const ComputedStyle* aStyle) const {
  return mBackgroundColor.CalcColor(*aStyle);
}

bool nsStyleBackground::IsTransparent(const nsIFrame* aFrame) const {
  return IsTransparent(aFrame->Style());
}

bool nsStyleBackground::IsTransparent(const ComputedStyle* aStyle) const {
  return BottomLayer().mImage.IsNone() && mImage.mImageCount == 1 &&
         NS_GET_A(BackgroundColor(aStyle)) == 0;
}

StyleTransition::StyleTransition(const StyleTransition& aCopy) = default;

bool StyleTransition::operator==(const StyleTransition& aOther) const {
  return mTimingFunction == aOther.mTimingFunction &&
         mDuration == aOther.mDuration && mDelay == aOther.mDelay &&
         mProperty == aOther.mProperty && mBehavior == aOther.mBehavior;
}

StyleAnimation::StyleAnimation(const StyleAnimation& aCopy) = default;

bool StyleAnimation::operator==(const StyleAnimation& aOther) const {
  return mTimingFunction == aOther.mTimingFunction &&
         mDuration == aOther.mDuration && mDelay == aOther.mDelay &&
         mName == aOther.mName && mDirection == aOther.mDirection &&
         mFillMode == aOther.mFillMode && mPlayState == aOther.mPlayState &&
         mIterationCount == aOther.mIterationCount &&
         mComposition == aOther.mComposition && mTimeline == aOther.mTimeline &&
         mRangeStart == aOther.mRangeStart && mRangeEnd == aOther.mRangeEnd;
}

// --------------------
// nsStyleDisplay
//
nsStyleDisplay::nsStyleDisplay()
    : mDisplay(StyleDisplay::Inline),
      mOriginalDisplay(StyleDisplay::Inline),
      mContentVisibility(StyleContentVisibility::Visible),
      mContainerType(StyleContainerType::NORMAL),
      mAppearance(StyleAppearance::None),
      mContain(StyleContain::NONE),
      mEffectiveContainment(StyleContain::NONE),
      mDefaultAppearance(StyleAppearance::None),
      mPosition(StylePositionProperty::Static),
      mFloat(StyleFloat::None),
      mClear(StyleClear::None),
      mBreakInside(StyleBreakWithin::Auto),
      mBreakBefore(StyleBreakBetween::Auto),
      mBreakAfter(StyleBreakBetween::Auto),
      mOverflowX(StyleOverflow::Visible),
      mOverflowY(StyleOverflow::Visible),
      mScrollbarGutter(StyleScrollbarGutter::AUTO),
      mResize(StyleResize::None),
      mOrient(StyleOrient::Inline),
      mIsolation(StyleIsolation::Auto),
      mTopLayer(StyleTopLayer::None),
      mTouchAction(StyleTouchAction::AUTO),
      mScrollBehavior(StyleScrollBehavior::Auto),
      mOverscrollBehaviorX(StyleOverscrollBehavior::Auto),
      mOverscrollBehaviorY(StyleOverscrollBehavior::Auto),
      mOverflowAnchor(StyleOverflowAnchor::Auto),
      mScrollSnapAlign{StyleScrollSnapAlignKeyword::None,
                       StyleScrollSnapAlignKeyword::None},
      mScrollSnapStop{StyleScrollSnapStop::Normal},
      mScrollSnapType{StyleScrollSnapAxis::Both,
                      StyleScrollSnapStrictness::None},
      mBackfaceVisibility(StyleBackfaceVisibility::Visible),
      mTransformStyle(StyleTransformStyle::Flat),
      mTransformBox(StyleTransformBox::ViewBox),
      mRotate(StyleRotate::None()),
      mTranslate(StyleTranslate::None()),
      mScale(StyleScale::None()),
      mWillChange{{}, {0}},
      mOffsetPath(StyleOffsetPath::None()),
      mOffsetDistance(LengthPercentage::Zero()),
      mOffsetRotate{true, StyleAngle{0.0}},
      mOffsetAnchor(StylePositionOrAuto::Auto()),
      mOffsetPosition(StyleOffsetPosition::Normal()),
      mTransformOrigin{LengthPercentage::FromPercentage(0.5),
                       LengthPercentage::FromPercentage(0.5),
                       {0.}},
      mChildPerspective(StylePerspective::None()),
      mPerspectiveOrigin(Position::FromPercentage(0.5f)),
      mAlignmentBaseline(StyleAlignmentBaseline::Baseline),
      mBaselineShift(StyleBaselineShift::Length(LengthPercentage::Zero())),
      mBaselineSource(StyleBaselineSource::Auto),
      mWebkitLineClamp(0),
      mShapeMargin(LengthPercentage::Zero()),
      mShapeOutside(StyleShapeOutside::None()) {
  MOZ_COUNT_CTOR(nsStyleDisplay);
}

nsStyleDisplay::nsStyleDisplay(const nsStyleDisplay& aSource)
    : mDisplay(aSource.mDisplay),
      mOriginalDisplay(aSource.mOriginalDisplay),
      mContentVisibility(aSource.mContentVisibility),
      mContainerType(aSource.mContainerType),
      mAppearance(aSource.mAppearance),
      mContain(aSource.mContain),
      mEffectiveContainment(aSource.mEffectiveContainment),
      mDefaultAppearance(aSource.mDefaultAppearance),
      mPosition(aSource.mPosition),
      mFloat(aSource.mFloat),
      mClear(aSource.mClear),
      mBreakInside(aSource.mBreakInside),
      mBreakBefore(aSource.mBreakBefore),
      mBreakAfter(aSource.mBreakAfter),
      mOverflowX(aSource.mOverflowX),
      mOverflowY(aSource.mOverflowY),
      mScrollbarGutter(aSource.mScrollbarGutter),
      mResize(aSource.mResize),
      mOrient(aSource.mOrient),
      mIsolation(aSource.mIsolation),
      mTopLayer(aSource.mTopLayer),
      mTouchAction(aSource.mTouchAction),
      mScrollBehavior(aSource.mScrollBehavior),
      mOverscrollBehaviorX(aSource.mOverscrollBehaviorX),
      mOverscrollBehaviorY(aSource.mOverscrollBehaviorY),
      mOverflowAnchor(aSource.mOverflowAnchor),
      mScrollSnapAlign(aSource.mScrollSnapAlign),
      mScrollSnapStop(aSource.mScrollSnapStop),
      mScrollSnapType(aSource.mScrollSnapType),
      mBackfaceVisibility(aSource.mBackfaceVisibility),
      mTransformStyle(aSource.mTransformStyle),
      mTransformBox(aSource.mTransformBox),
      mTransform(aSource.mTransform),
      mRotate(aSource.mRotate),
      mTranslate(aSource.mTranslate),
      mScale(aSource.mScale),
      mContainerName(aSource.mContainerName),
      mWillChange(aSource.mWillChange),
      mOffsetPath(aSource.mOffsetPath),
      mOffsetDistance(aSource.mOffsetDistance),
      mOffsetRotate(aSource.mOffsetRotate),
      mOffsetAnchor(aSource.mOffsetAnchor),
      mOffsetPosition(aSource.mOffsetPosition),
      mTransformOrigin(aSource.mTransformOrigin),
      mChildPerspective(aSource.mChildPerspective),
      mPerspectiveOrigin(aSource.mPerspectiveOrigin),
      mAlignmentBaseline(aSource.mAlignmentBaseline),
      mBaselineShift(aSource.mBaselineShift),
      mBaselineSource(aSource.mBaselineSource),
      mWebkitLineClamp(aSource.mWebkitLineClamp),
      mShapeImageThreshold(aSource.mShapeImageThreshold),
      mShapeMargin(aSource.mShapeMargin),
      mShapeOutside(aSource.mShapeOutside),
      mAnchorName(aSource.mAnchorName),
      mAnchorScope(aSource.mAnchorScope) {
  MOZ_COUNT_CTOR(nsStyleDisplay);
}

void nsStyleDisplay::TriggerImageLoads(Document& aDocument,
                                       const nsStyleDisplay* aOldStyle) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mShapeOutside.IsImage()) {
    const auto* old = aOldStyle && aOldStyle->mShapeOutside.IsImage()
                          ? &aOldStyle->mShapeOutside.AsImage()
                          : nullptr;
    // Const-cast is ugly but legit, we could avoid it by generating mut-casts
    // with cbindgen.
    const_cast<StyleImage&>(mShapeOutside.AsImage())
        .ResolveImage(aDocument, old);
  }
}

template <typename TransformLike>
static inline nsChangeHint CompareTransformValues(
    const TransformLike& aOldTransform, const TransformLike& aNewTransform) {
  nsChangeHint result = nsChangeHint(0);

  // Note: If we add a new change hint for transform changes here, we have to
  // modify KeyframeEffect::CalculateCumulativeChangeHint too!
  if (aOldTransform != aNewTransform) {
    result |= nsChangeHint_UpdateTransformLayer;
    if (!aOldTransform.IsNone() && !aNewTransform.IsNone()) {
      result |= nsChangeHint_UpdatePostTransformOverflow;
    } else {
      result |= nsChangeHint_UpdateOverflow;
    }
  }

  return result;
}

static inline nsChangeHint CompareMotionValues(
    const nsStyleDisplay& aDisplay, const nsStyleDisplay& aNewDisplay) {
  if (aDisplay.mOffsetPath == aNewDisplay.mOffsetPath) {
    if (aDisplay.mOffsetDistance == aNewDisplay.mOffsetDistance &&
        aDisplay.mOffsetRotate == aNewDisplay.mOffsetRotate &&
        aDisplay.mOffsetAnchor == aNewDisplay.mOffsetAnchor &&
        aDisplay.mOffsetPosition == aNewDisplay.mOffsetPosition) {
      return nsChangeHint(0);
    }

    // No motion path transform is applied.
    if (aDisplay.mOffsetPath.IsNone()) {
      return nsChangeHint_NeutralChange;
    }
  }

  // TODO: Bug 1482737: This probably doesn't need to UpdateOverflow
  // (or UpdateTransformLayer) if there's already a transform.
  // Set the same hints as what we use for transform because motion path is
  // a kind of transform and will be combined with other transforms.
  nsChangeHint result = nsChangeHint_UpdateTransformLayer;
  if (!aDisplay.mOffsetPath.IsNone() && !aNewDisplay.mOffsetPath.IsNone()) {
    result |= nsChangeHint_UpdatePostTransformOverflow;
  } else {
    result |= nsChangeHint_UpdateOverflow;
  }
  return result;
}

static bool ScrollbarGenerationChanged(const nsStyleDisplay& aOld,
                                       const nsStyleDisplay& aNew) {
  auto changed = [](StyleOverflow aOld, StyleOverflow aNew) {
    return aOld != aNew &&
           (aOld == StyleOverflow::Hidden || aNew == StyleOverflow::Hidden);
  };
  return changed(aOld.mOverflowX, aNew.mOverflowX) ||
         changed(aOld.mOverflowY, aNew.mOverflowY);
}

static bool AppearanceValueAffectsFrames(StyleAppearance aAppearance,
                                         StyleAppearance aDefaultAppearance) {
  switch (aAppearance) {
    case StyleAppearance::Base:
      return aDefaultAppearance == StyleAppearance::Menulist ||
             aDefaultAppearance == StyleAppearance::Listbox ||
             aDefaultAppearance == StyleAppearance::Checkbox ||
             aDefaultAppearance == StyleAppearance::Radio;
    case StyleAppearance::BaseSelect:
      // <select> doesn't construct an nsComboboxControlFrame with base
      // appearance.
      return aDefaultAppearance == StyleAppearance::Menulist ||
             aDefaultAppearance == StyleAppearance::Listbox;
    case StyleAppearance::None:
      // Checkbox / radio with appearance: none don't construct an
      // nsCheckboxRadioFrame.
      return aDefaultAppearance == StyleAppearance::Checkbox ||
             aDefaultAppearance == StyleAppearance::Radio;
    case StyleAppearance::Textfield:
      // This is for <input type=number/search> where we allow authors to
      // specify a |-moz-appearance:textfield| to get a control without buttons.
      // We need to reframe since this affects the spinbox creation in
      // nsNumberControlFrame::CreateAnonymousContent.
      return aDefaultAppearance == StyleAppearance::NumberInput ||
             aDefaultAppearance == StyleAppearance::PasswordInput;
    case StyleAppearance::Menulist:
      // This affects the menulist button creation.
      return aDefaultAppearance == StyleAppearance::Menulist;
    default:
      return false;
  }
}

nsChangeHint nsStyleDisplay::CalcDifference(
    const nsStyleDisplay& aNewData, const ComputedStyle& aOldStyle) const {
  if (mDisplay != aNewData.mDisplay ||
      (mFloat == StyleFloat::None) != (aNewData.mFloat == StyleFloat::None) ||
      mTopLayer != aNewData.mTopLayer || mResize != aNewData.mResize) {
    return nsChangeHint_ReconstructFrame;
  }

  auto oldAppearance = EffectiveAppearance();
  auto newAppearance = aNewData.EffectiveAppearance();
  if (oldAppearance != newAppearance) {
    // Changes to the mDefaultAppearance values handled in
    // AppearanceValueAffectsFrames reconstruct their frames via other means.
    // E.g. switching the <input> type attribute reframes via
    // GetAttributeChangeHint. Thus, it doesn't matter whether we pick
    // mDefaultAppearance or aNewData.mDefaultAppearance for the check below.
    if (AppearanceValueAffectsFrames(oldAppearance, mDefaultAppearance) ||
        AppearanceValueAffectsFrames(newAppearance, mDefaultAppearance)) {
      return nsChangeHint_ReconstructFrame;
    }
  }

  auto hint = nsChangeHint(0);
  const auto containmentDiff =
      mEffectiveContainment ^ aNewData.mEffectiveContainment;
  if (containmentDiff) {
    if (containmentDiff & StyleContain::STYLE) {
      // Style containment affects counters so we need to re-frame.
      return nsChangeHint_ReconstructFrame;
    }
    if (containmentDiff & (StyleContain::PAINT | StyleContain::LAYOUT)) {
      // Paint and layout containment boxes are absolutely/fixed positioning
      // containers.
      hint |= nsChangeHint_UpdateContainingBlock;
    }
    // The other container types only need a reflow.
    hint |= nsChangeHint_AllReflowHints | nsChangeHint_RepaintFrame;
  }
  if (mPosition != aNewData.mPosition) {
    if (IsAbsolutelyPositionedStyle() ||
        aNewData.IsAbsolutelyPositionedStyle()) {
      // This changes our parent relationship on the frame tree and / or needs
      // to create a placeholder, so gotta reframe. There are some cases (when
      // switching from fixed to absolute or viceversa, if our containing block
      // happens to remain the same, i.e., if it has a transform or such) where
      // this wouldn't really be needed (though we'd still need to move the
      // frame from one child list to another). In any case we don't have a hand
      // to that information from here, and it doesn't seem like a case worth
      // optimizing for.
      return nsChangeHint_ReconstructFrame;
    }
    // We start or stop being a containing block for abspos descendants. This
    // also causes painting to change, as we'd become a pseudo-stacking context.
    if (IsRelativelyOrStickyPositionedStyle() !=
        aNewData.IsRelativelyOrStickyPositionedStyle()) {
      hint |= nsChangeHint_UpdateContainingBlock | nsChangeHint_RepaintFrame;
    }
    if (IsPositionForcingStackingContext() !=
        aNewData.IsPositionForcingStackingContext()) {
      hint |= nsChangeHint_RepaintFrame;
    }
    // On top of that: if the above ends up not reframing, we need a reflow to
    // compute our relative, static or sticky position.
    hint |= nsChangeHint_NeedReflow | nsChangeHint_ReflowChangesSizeOrPosition;
  }

  if (mScrollSnapAlign != aNewData.mScrollSnapAlign ||
      mScrollSnapType != aNewData.mScrollSnapType ||
      mScrollSnapStop != aNewData.mScrollSnapStop) {
    hint |= nsChangeHint_RepaintFrame;
  }
  if (mScrollBehavior != aNewData.mScrollBehavior) {
    hint |= nsChangeHint_NeutralChange;
  }

  if (mOverflowX != aNewData.mOverflowX || mOverflowY != aNewData.mOverflowY) {
    const bool isScrollable = IsScrollableOverflow();
    if (isScrollable != aNewData.IsScrollableOverflow()) {
      // We may need to construct or destroy a scroll frame as a result of this
      // change. If we don't, we still need to update our overflow in some cases
      // (like svg:foreignObject), which ignore the scrollable-ness of our
      // overflow.
      hint |= nsChangeHint_ScrollbarChange | nsChangeHint_UpdateOverflow |
              nsChangeHint_RepaintFrame;
    } else if (isScrollable) {
      if (ScrollbarGenerationChanged(*this, aNewData)) {
        // We might need to reframe in the case of hidden -> non-hidden case
        // though, since ScrollContainerFrame::CreateAnonymousContent avoids
        // creating scrollbars altogether for overflow: hidden. That seems it
        // could create some interesting perf cliffs...
        hint |= nsChangeHint_ScrollbarChange;
      } else {
        // Otherwise, for changes where both overflow values are scrollable,
        // means that scrollbars may appear or disappear. We need to reflow,
        // since reflow is what determines which scrollbars if any are visible.
        hint |= nsChangeHint_ReflowHintsForScrollbarChange;
      }
    } else {
      // Otherwise this is a change between 'visible' and 'clip'.
      // Here only whether we have a 'clip' changes, so just repaint and
      // update our overflow areas in that case.
      hint |= nsChangeHint_UpdateOverflow | nsChangeHint_RepaintFrame;
    }
  }

  if (mScrollbarGutter != aNewData.mScrollbarGutter) {
    if (IsScrollableOverflow() || aOldStyle.IsRootElementStyle()) {
      // Changing scrollbar-gutter affects available inline-size of a inner
      // scrolled frame, so we need a reflow for scrollbar change. Note that the
      // root is always scrollable in HTML, even if its style doesn't say so.
      hint |= nsChangeHint_ReflowHintsForScrollbarChange;
    } else {
      // scrollbar-gutter only applies to scroll containers.
      hint |= nsChangeHint_NeutralChange;
    }
  }

  if (mFloat != aNewData.mFloat) {
    // Changing which side we're floating on (float:none was handled above).
    hint |= nsChangeHint_ReflowHintsForFloatAreaChange;
  }

  if (mShapeOutside != aNewData.mShapeOutside ||
      mShapeMargin != aNewData.mShapeMargin ||
      mShapeImageThreshold != aNewData.mShapeImageThreshold) {
    if (aNewData.mFloat != StyleFloat::None) {
      // If we are floating, and our shape-outside, shape-margin, or
      // shape-image-threshold are changed, our descendants are not impacted,
      // but our ancestor and siblings are.
      hint |= nsChangeHint_ReflowHintsForFloatAreaChange;
    } else {
      // shape-outside or shape-margin or shape-image-threshold changed,
      // but we don't need to reflow because we're not floating.
      hint |= nsChangeHint_NeutralChange;
    }
  }

  if (mWebkitLineClamp != aNewData.mWebkitLineClamp ||
      mAlignmentBaseline != aNewData.mAlignmentBaseline ||
      mBaselineShift != aNewData.mBaselineShift ||
      mBaselineSource != aNewData.mBaselineSource) {
    // XXX Can this just be AllReflowHints + RepaintFrame, and be included in
    // the block below?
    hint |= NS_STYLE_HINT_REFLOW;
  }

  // XXX the following is conservative, for now: changing float breaking
  // shouldn't necessarily require a repaint, reflow should suffice.
  //
  // FIXME(emilio): We definitely change the frame tree in nsCSSFrameConstructor
  // based on break-before / break-after... Shouldn't that reframe?
  if (mClear != aNewData.mClear || mBreakInside != aNewData.mBreakInside ||
      mBreakBefore != aNewData.mBreakBefore ||
      mBreakAfter != aNewData.mBreakAfter ||
      mAppearance != aNewData.mAppearance ||
      mDefaultAppearance != aNewData.mDefaultAppearance ||
      mOrient != aNewData.mOrient) {
    hint |= nsChangeHint_AllReflowHints | nsChangeHint_RepaintFrame;
  }

  if (mIsolation != aNewData.mIsolation) {
    hint |= nsChangeHint_RepaintFrame;
  }

  /* If we've added or removed the transform property, we need to reconstruct
   * the frame to add or remove the view object, and also to handle abs-pos and
   * fixed-pos containers.
   */
  if (HasTransformStyle() != aNewData.HasTransformStyle()) {
    hint |= nsChangeHint_ComprehensiveAddOrRemoveTransform;
  } else {
    /* Otherwise, if we've kept the property lying around and we already had a
     * transform, we need to see whether or not we've changed the transform.
     * If so, we need to recompute its overflow rect (which probably changed
     * if the transform changed) and to redraw within the bounds of that new
     * overflow rect.
     *
     * If the property isn't present in either style struct, we still do the
     * comparisons but turn all the resulting change hints into
     * nsChangeHint_NeutralChange.
     */
    nsChangeHint transformHint = CalcTransformPropertyDifference(aNewData);

    if (transformHint) {
      if (HasTransformStyle()) {
        hint |= transformHint;
      } else {
        hint |= nsChangeHint_NeutralChange;
      }
    }
  }

  if (HasPerspectiveStyle() != aNewData.HasPerspectiveStyle()) {
    // A change from/to being a containing block for position:fixed.
    hint |= nsChangeHint_UpdateContainingBlock | nsChangeHint_UpdateOverflow |
            nsChangeHint_RepaintFrame;
  } else if (mChildPerspective != aNewData.mChildPerspective) {
    hint |= nsChangeHint_UpdateOverflow | nsChangeHint_RepaintFrame;
  }

  // Note that the HasTransformStyle() != aNewData.HasTransformStyle()
  // test above handles relevant changes in the StyleWillChangeBit_TRANSFORM
  // bit, which in turn handles frame reconstruction for changes in the
  // containing block of fixed-positioned elements.
  auto willChangeBitsChanged = mWillChange.bits ^ aNewData.mWillChange.bits;

  if (willChangeBitsChanged &
      (StyleWillChangeBits::STACKING_CONTEXT_UNCONDITIONAL |
       StyleWillChangeBits::SCROLL | StyleWillChangeBits::OPACITY |
       StyleWillChangeBits::PERSPECTIVE | StyleWillChangeBits::TRANSFORM |
       StyleWillChangeBits::Z_INDEX)) {
    hint |= nsChangeHint_RepaintFrame;
  }

  if (willChangeBitsChanged &
      (StyleWillChangeBits::FIXPOS_CB_NON_SVG | StyleWillChangeBits::TRANSFORM |
       StyleWillChangeBits::PERSPECTIVE | StyleWillChangeBits::POSITION |
       StyleWillChangeBits::CONTAIN)) {
    hint |= nsChangeHint_UpdateContainingBlock;
  }

  // If touch-action is changed, we need to regenerate the event regions on
  // the layers and send it over to the compositor for APZ to handle.
  if (mTouchAction != aNewData.mTouchAction) {
    hint |= nsChangeHint_RepaintFrame;
  }

  // If overscroll-behavior has changed, the changes are picked up
  // during a repaint.
  if (mOverscrollBehaviorX != aNewData.mOverscrollBehaviorX ||
      mOverscrollBehaviorY != aNewData.mOverscrollBehaviorY) {
    hint |= nsChangeHint_SchedulePaint;
  }

  if (mOriginalDisplay != aNewData.mOriginalDisplay) {
    // Our hypothetical box position may have changed.
    //
    // Note that it doesn't matter if we look at the old or the new struct,
    // since a change on whether we need a hypothetical position would trigger
    // reflow anyway.
    if (IsAbsolutelyPositionedStyle() &&
        aOldStyle.StylePosition()->NeedsHypotheticalPositionIfAbsPos()) {
      hint |=
          nsChangeHint_NeedReflow | nsChangeHint_ReflowChangesSizeOrPosition;
    } else {
      hint |= nsChangeHint_NeutralChange;
    }
  }

  // Note:  Our current behavior for handling changes to the
  // transition-duration, transition-delay, and transition-timing-function
  // properties is to do nothing.  In other words, the transition
  // property that matters is what it is when the transition begins, and
  // we don't stop a transition later because the transition property
  // changed.
  // We do handle changes to transition-property, but we don't need to
  // bother with anything here, since the transition manager is notified
  // of any ComputedStyle change anyway.

  // Note: Likewise, for animation-*, the animation manager gets
  // notified about every new ComputedStyle constructed, and it uses
  // that opportunity to handle dynamic changes appropriately.

  // But we still need to return nsChangeHint_NeutralChange for these
  // properties, since some data did change in the style struct.

  // TODO(emilio): Figure out change hints for container-name, maybe it needs to
  // be handled by the style system as a special-case (since it changes
  // container-query selection on descendants).
  // container-type / contain / content-visibility are handled by the
  // mEffectiveContainment check.
  if (!hint && (mWillChange != aNewData.mWillChange ||
                mOverflowAnchor != aNewData.mOverflowAnchor ||
                mContentVisibility != aNewData.mContentVisibility ||
                mContainerType != aNewData.mContainerType ||
                mContain != aNewData.mContain ||
                mContainerName != aNewData.mContainerName ||
                mAnchorName != aNewData.mAnchorName ||
                mAnchorScope != aNewData.mAnchorScope)) {
    hint |= nsChangeHint_NeutralChange;
  }

  return hint;
}

nsChangeHint nsStyleDisplay::CalcTransformPropertyDifference(
    const nsStyleDisplay& aNewData) const {
  nsChangeHint transformHint = nsChangeHint(0);

  transformHint |= CompareTransformValues(mTransform, aNewData.mTransform);
  transformHint |= CompareTransformValues(mRotate, aNewData.mRotate);
  transformHint |= CompareTransformValues(mTranslate, aNewData.mTranslate);
  transformHint |= CompareTransformValues(mScale, aNewData.mScale);
  transformHint |= CompareMotionValues(*this, aNewData);

  if (mTransformOrigin != aNewData.mTransformOrigin) {
    transformHint |= nsChangeHint_UpdateTransformLayer |
                     nsChangeHint_UpdatePostTransformOverflow;
  }

  if (mPerspectiveOrigin != aNewData.mPerspectiveOrigin ||
      mTransformStyle != aNewData.mTransformStyle ||
      mTransformBox != aNewData.mTransformBox) {
    transformHint |= nsChangeHint_UpdateOverflow | nsChangeHint_RepaintFrame;
  }

  if (mBackfaceVisibility != aNewData.mBackfaceVisibility) {
    transformHint |= nsChangeHint_RepaintFrame;
  }

  return transformHint;
}

// --------------------
// nsStyleVisibility
//

nsStyleVisibility::nsStyleVisibility(const Document& aDocument)
    : mDirection(aDocument.GetBidiOptions() == IBMBIDI_TEXTDIRECTION_RTL
                     ? StyleDirection::Rtl
                     : StyleDirection::Ltr),
      mVisible(StyleVisibility::Visible),
      mImageRendering(StyleImageRendering::Auto),
      mWritingMode(StyleWritingModeProperty::HorizontalTb),
      mTextOrientation(StyleTextOrientation::Mixed),
      mMozBoxCollapse(StyleBoxCollapse::Flex),
      mPrintColorAdjust(StylePrintColorAdjust::Economy),
      mDominantBaseline(StyleDominantBaseline::Auto),
      mImageOrientation(StyleImageOrientation::FromImage) {
  MOZ_COUNT_CTOR(nsStyleVisibility);
}

nsStyleVisibility::nsStyleVisibility(const nsStyleVisibility& aSource)
    : mDirection(aSource.mDirection),
      mVisible(aSource.mVisible),
      mImageRendering(aSource.mImageRendering),
      mWritingMode(aSource.mWritingMode),
      mTextOrientation(aSource.mTextOrientation),
      mMozBoxCollapse(aSource.mMozBoxCollapse),
      mPrintColorAdjust(aSource.mPrintColorAdjust),
      mDominantBaseline(aSource.mDominantBaseline),
      mImageOrientation(aSource.mImageOrientation) {
  MOZ_COUNT_CTOR(nsStyleVisibility);
}

nsChangeHint nsStyleVisibility::CalcDifference(
    const nsStyleVisibility& aNewData) const {
  nsChangeHint hint = nsChangeHint(0);

  if (mDirection != aNewData.mDirection ||
      mWritingMode != aNewData.mWritingMode ||
      mTextOrientation != aNewData.mTextOrientation) {
    // It's important that a change in mWritingMode results in frame
    // reconstruction, because it may affect intrinsic size (see
    // nsSubDocumentFrame::GetIntrinsicISize/BSize).
    // Also, the used WritingMode value is now a field on nsIFrame and some
    // classes (e.g. table rows/cells) copy their value from an ancestor, and
    // that is a combo of direction+writing-mode+text-orientation.
    return nsChangeHint_ReconstructFrame;
  }
  if (mImageOrientation != aNewData.mImageOrientation) {
    hint |= nsChangeHint_AllReflowHints | nsChangeHint_RepaintFrame;
  }
  if (mVisible != aNewData.mVisible) {
    if (mVisible == StyleVisibility::Visible ||
        aNewData.mVisible == StyleVisibility::Visible) {
      hint |= nsChangeHint_VisibilityChange;
    }
    if (mVisible == StyleVisibility::Collapse ||
        aNewData.mVisible == StyleVisibility::Collapse) {
      hint |= NS_STYLE_HINT_REFLOW;
    } else {
      hint |= NS_STYLE_HINT_VISUAL;
    }
  }
  if (mMozBoxCollapse != aNewData.mMozBoxCollapse ||
      mDominantBaseline != aNewData.mDominantBaseline) {
    hint |= NS_STYLE_HINT_REFLOW;
  }
  if (mImageRendering != aNewData.mImageRendering) {
    hint |= nsChangeHint_RepaintFrame;
  }
  if (mPrintColorAdjust != aNewData.mPrintColorAdjust) {
    // color-adjust only affects media where dynamic changes can't happen.
    hint |= nsChangeHint_NeutralChange;
  }
  return hint;
}

StyleImageOrientation nsStyleVisibility::UsedImageOrientation(
    imgIRequest* aRequest, StyleImageOrientation aOrientation) {
  if (aOrientation == StyleImageOrientation::FromImage || !aRequest) {
    return aOrientation;
  }

  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      aRequest->GetTriggeringPrincipal();

  // If the request was for a blob, the request may not have a triggering
  // principal and we should use the input orientation.
  if (!triggeringPrincipal) {
    return aOrientation;
  }

  nsCOMPtr<nsIURI> uri = aRequest->GetURI();
  // If the image request is a data uri, then treat the request as a
  // same origin request.
  bool isSameOrigin =
      uri->SchemeIs("data") || triggeringPrincipal->IsSameOrigin(uri);

  // If the image request is a cross-origin request that does not use CORS,
  // do not enforce the image orientation found in the style. Use the image
  // orientation found in the exif data.
  if (!isSameOrigin && !nsLayoutUtils::ImageRequestUsesCORS(aRequest)) {
    return StyleImageOrientation::FromImage;
  }

  return aOrientation;
}

//-----------------------
// nsStyleContent
//

nsStyleContent::nsStyleContent() : mContent(StyleContent::Normal()) {
  MOZ_COUNT_CTOR(nsStyleContent);
}

nsStyleContent::nsStyleContent(const nsStyleContent& aSource)
    : mContent(aSource.mContent),
      mCounterIncrement(aSource.mCounterIncrement),
      mCounterReset(aSource.mCounterReset),
      mCounterSet(aSource.mCounterSet) {
  MOZ_COUNT_CTOR(nsStyleContent);
}

nsChangeHint nsStyleContent::CalcDifference(
    const nsStyleContent& aNewData) const {
  // Unfortunately we need to reframe even if the content lengths are the same;
  // a simple reflow will not pick up different text or different image URLs,
  // since we set all that up in the CSSFrameConstructor
  if (mContent != aNewData.mContent ||
      mCounterIncrement != aNewData.mCounterIncrement ||
      mCounterReset != aNewData.mCounterReset ||
      mCounterSet != aNewData.mCounterSet) {
    return nsChangeHint_ReconstructFrame;
  }

  return nsChangeHint(0);
}

void nsStyleContent::TriggerImageLoads(Document& aDoc,
                                       const nsStyleContent* aOld) {
  if (!mContent.IsItems()) {
    return;
  }

  Span<const StyleContentItem> oldItems;
  if (aOld) {
    oldItems = aOld->NonAltContentItems();
  }

  auto items = NonAltContentItems();
  for (size_t i = 0; i < items.Length(); ++i) {
    const auto& item = items[i];
    if (!item.IsImage()) {
      continue;
    }
    const auto& image = item.AsImage();
    const auto* oldImage = i < oldItems.Length() && oldItems[i].IsImage()
                               ? &oldItems[i].AsImage()
                               : nullptr;
    const_cast<StyleImage&>(image).ResolveImage(aDoc, oldImage);
  }
}

// --------------------
// nsStyleTextReset
//

nsStyleTextReset::nsStyleTextReset()
    : mTextDecorationLine(StyleTextDecorationLine::NONE),
      mTextDecorationStyle(StyleTextDecorationStyle::Solid),
      mUnicodeBidi(StyleUnicodeBidi::Normal),
      mInitialLetter{0, 0},
      mTextDecorationColor(StyleColor::CurrentColor()),
      mTextDecorationThickness(StyleTextDecorationLength::Auto()),
      mTextDecorationInset(StyleTextDecorationInset::Length(
          StyleLength::Zero(), StyleLength::Zero())),
      mTextBoxTrim(StyleTextBoxTrim::NONE) {
  MOZ_COUNT_CTOR(nsStyleTextReset);
}

nsStyleTextReset::nsStyleTextReset(const nsStyleTextReset& aSource)
    : mTextOverflow(aSource.mTextOverflow),
      mTextDecorationLine(aSource.mTextDecorationLine),
      mTextDecorationStyle(aSource.mTextDecorationStyle),
      mUnicodeBidi(aSource.mUnicodeBidi),
      mInitialLetter(aSource.mInitialLetter),
      mTextDecorationColor(aSource.mTextDecorationColor),
      mTextDecorationThickness(aSource.mTextDecorationThickness),
      mTextDecorationInset(aSource.mTextDecorationInset),
      mTextBoxTrim(aSource.mTextBoxTrim) {
  MOZ_COUNT_CTOR(nsStyleTextReset);
}

nsChangeHint nsStyleTextReset::CalcDifference(
    const nsStyleTextReset& aNewData) const {
  if (mUnicodeBidi != aNewData.mUnicodeBidi ||
      mInitialLetter != aNewData.mInitialLetter ||
      mTextBoxTrim != aNewData.mTextBoxTrim) {
    return NS_STYLE_HINT_REFLOW;
  }

  if (mTextDecorationLine != aNewData.mTextDecorationLine ||
      mTextDecorationStyle != aNewData.mTextDecorationStyle ||
      mTextDecorationThickness != aNewData.mTextDecorationThickness ||
      mTextDecorationInset != aNewData.mTextDecorationInset) {
    // Changes to our text-decoration line can impact our overflow area &
    // also our descendants' overflow areas (particularly for text-frame
    // descendants).  So, we update those areas & trigger a repaint.
    return nsChangeHint_RepaintFrame | nsChangeHint_UpdateSubtreeOverflow |
           nsChangeHint_SchedulePaint;
  }

  // Repaint for decoration color changes or text-overflow.
  if (mTextDecorationColor != aNewData.mTextDecorationColor ||
      mTextOverflow != aNewData.mTextOverflow) {
    return nsChangeHint_RepaintFrame;
  }

  return nsChangeHint(0);
}

// --------------------
// nsStyleText
//

static StyleAbsoluteColor DefaultColor(const Document& aDocument) {
  return StyleAbsoluteColor::FromColor(
      PreferenceSheet::PrefsFor(aDocument)
          .ColorsFor(aDocument.DefaultColorScheme())
          .mDefault);
}

nsStyleText::nsStyleText(const Document& aDocument)
    : mColor(DefaultColor(aDocument)),
      mForcedColorAdjust(StyleForcedColorAdjust::Auto),
      mTextTransform(StyleTextTransform::NONE),
      mTextAlign(StyleTextAlign::Start),
      mTextAlignLast(StyleTextAlignLast::Auto),
      mTextJustify(StyleTextJustify::Auto),
      mHyphens(StyleHyphens::Manual),
      mRubyAlign(StyleRubyAlign::SpaceAround),
      mRubyPosition(StyleRubyPosition::AlternateOver),
      mTextSizeAdjust(StyleTextSizeAdjust::Auto),
      mTextCombineUpright(StyleTextCombineUpright::None),
      mMozControlCharacterVisibility(
          StaticPrefs::layout_css_control_characters_visible()
              ? StyleMozControlCharacterVisibility::Visible
              : StyleMozControlCharacterVisibility::Hidden),
      mTextEmphasisPosition(StyleTextEmphasisPosition::AUTO),
      mTextRendering(StyleTextRendering::Auto),
      mTextEmphasisColor(StyleColor::CurrentColor()),
      mWebkitTextFillColor(StyleColor::CurrentColor()),
      mWebkitTextStrokeColor(StyleColor::CurrentColor()),
      mTabSize(StyleNonNegativeLengthOrNumber::Number(8.f)),
      mWordSpacing(LengthPercentage::Zero()),
      mLetterSpacing(LengthPercentage::Zero()),
      mTextBoxEdge(StyleTextBoxEdge::Auto()),
      mTextUnderlineOffset(LengthPercentageOrAuto::Auto()),
      mTextDecorationSkipInk(StyleTextDecorationSkipInk::Auto),
      mTextUnderlinePosition(StyleTextUnderlinePosition::AUTO),
      mWebkitTextStrokeWidth(0),
      mTextEmphasisStyle(StyleTextEmphasisStyle::None()) {
  MOZ_COUNT_CTOR(nsStyleText);
}

nsStyleText::nsStyleText(const nsStyleText& aSource)
    : mColor(aSource.mColor),
      mForcedColorAdjust(aSource.mForcedColorAdjust),
      mTextTransform(aSource.mTextTransform),
      mTextAlign(aSource.mTextAlign),
      mTextAlignLast(aSource.mTextAlignLast),
      mTextJustify(aSource.mTextJustify),
      mWhiteSpaceCollapse(aSource.mWhiteSpaceCollapse),
      mTextWrapMode(aSource.mTextWrapMode),
      mLineBreak(aSource.mLineBreak),
      mWordBreak(aSource.mWordBreak),
      mOverflowWrap(aSource.mOverflowWrap),
      mTextAutospace(aSource.mTextAutospace),
      mHyphens(aSource.mHyphens),
      mRubyAlign(aSource.mRubyAlign),
      mRubyPosition(aSource.mRubyPosition),
      mTextSizeAdjust(aSource.mTextSizeAdjust),
      mTextCombineUpright(aSource.mTextCombineUpright),
      mMozControlCharacterVisibility(aSource.mMozControlCharacterVisibility),
      mTextEmphasisPosition(aSource.mTextEmphasisPosition),
      mTextRendering(aSource.mTextRendering),
      mTextEmphasisColor(aSource.mTextEmphasisColor),
      mWebkitTextFillColor(aSource.mWebkitTextFillColor),
      mWebkitTextStrokeColor(aSource.mWebkitTextStrokeColor),
      mTabSize(aSource.mTabSize),
      mWordSpacing(aSource.mWordSpacing),
      mLetterSpacing(aSource.mLetterSpacing),
      mTextIndent(aSource.mTextIndent),
      mTextBoxEdge(aSource.mTextBoxEdge),
      mTextUnderlineOffset(aSource.mTextUnderlineOffset),
      mTextDecorationSkipInk(aSource.mTextDecorationSkipInk),
      mTextUnderlinePosition(aSource.mTextUnderlinePosition),
      mWebkitTextStrokeWidth(aSource.mWebkitTextStrokeWidth),
      mTextShadow(aSource.mTextShadow),
      mTextEmphasisStyle(aSource.mTextEmphasisStyle),
      mHyphenateCharacter(aSource.mHyphenateCharacter),
      mHyphenateLimitChars(aSource.mHyphenateLimitChars),
      mWebkitTextSecurity(aSource.mWebkitTextSecurity),
      mTextWrapStyle(aSource.mTextWrapStyle) {
  MOZ_COUNT_CTOR(nsStyleText);
}

nsChangeHint nsStyleText::CalcDifference(const nsStyleText& aNewData) const {
  if (WhiteSpaceOrNewlineIsSignificant() !=
      aNewData.WhiteSpaceOrNewlineIsSignificant()) {
    // This may require construction of suppressed text frames
    return nsChangeHint_ReconstructFrame;
  }

  if (mTextCombineUpright != aNewData.mTextCombineUpright ||
      mMozControlCharacterVisibility !=
          aNewData.mMozControlCharacterVisibility) {
    return nsChangeHint_ReconstructFrame;
  }

  if ((mTextAlign != aNewData.mTextAlign) ||
      (mTextAlignLast != aNewData.mTextAlignLast) ||
      (mTextTransform != aNewData.mTextTransform) ||
      (mWhiteSpaceCollapse != aNewData.mWhiteSpaceCollapse) ||
      (mTextWrapMode != aNewData.mTextWrapMode) ||
      (mLineBreak != aNewData.mLineBreak) ||
      (mWordBreak != aNewData.mWordBreak) ||
      (mOverflowWrap != aNewData.mOverflowWrap) ||
      (mHyphens != aNewData.mHyphens) || (mRubyAlign != aNewData.mRubyAlign) ||
      (mRubyPosition != aNewData.mRubyPosition) ||
      (mTextSizeAdjust != aNewData.mTextSizeAdjust) ||
      (mLetterSpacing != aNewData.mLetterSpacing) ||
      (mTextIndent != aNewData.mTextIndent) ||
      (mTextBoxEdge != aNewData.mTextBoxEdge) ||
      (mTextJustify != aNewData.mTextJustify) ||
      (mWordSpacing != aNewData.mWordSpacing) ||
      (mTabSize != aNewData.mTabSize) ||
      (mHyphenateCharacter != aNewData.mHyphenateCharacter) ||
      (mHyphenateLimitChars != aNewData.mHyphenateLimitChars) ||
      (mWebkitTextSecurity != aNewData.mWebkitTextSecurity) ||
      (mTextWrapStyle != aNewData.mTextWrapStyle) ||
      (mTextAutospace != aNewData.mTextAutospace)) {
    return NS_STYLE_HINT_REFLOW;
  }

  if (HasEffectiveTextEmphasis() != aNewData.HasEffectiveTextEmphasis() ||
      (HasEffectiveTextEmphasis() &&
       mTextEmphasisPosition != aNewData.mTextEmphasisPosition)) {
    // Text emphasis position change could affect line height calculation.
    return nsChangeHint_AllReflowHints | nsChangeHint_RepaintFrame;
  }

  nsChangeHint hint = nsChangeHint(0);

  // text-rendering changes require a reflow since they change SVG
  // frames' rects.
  if (mTextRendering != aNewData.mTextRendering) {
    hint |= nsChangeHint_NeedReflow | nsChangeHint_RepaintFrame;
  }

  if (mTextShadow != aNewData.mTextShadow ||
      mTextEmphasisStyle != aNewData.mTextEmphasisStyle ||
      mWebkitTextStrokeWidth != aNewData.mWebkitTextStrokeWidth ||
      mTextUnderlineOffset != aNewData.mTextUnderlineOffset ||
      mTextDecorationSkipInk != aNewData.mTextDecorationSkipInk ||
      mTextUnderlinePosition != aNewData.mTextUnderlinePosition) {
    hint |= nsChangeHint_UpdateSubtreeOverflow | nsChangeHint_SchedulePaint |
            nsChangeHint_RepaintFrame;

    // We don't add any other hints below.
    return hint;
  }

  if (mColor != aNewData.mColor) {
    hint |= nsChangeHint_RepaintFrame;
  }

  if (mTextEmphasisColor != aNewData.mTextEmphasisColor ||
      mWebkitTextFillColor != aNewData.mWebkitTextFillColor ||
      mWebkitTextStrokeColor != aNewData.mWebkitTextStrokeColor) {
    hint |= nsChangeHint_SchedulePaint | nsChangeHint_RepaintFrame;
  }

  if (hint) {
    return hint;
  }

  if (mTextEmphasisPosition != aNewData.mTextEmphasisPosition ||
      mForcedColorAdjust != aNewData.mForcedColorAdjust) {
    return nsChangeHint_NeutralChange;
  }

  return nsChangeHint(0);
}

LogicalSide nsStyleText::TextEmphasisSide(WritingMode aWM,
                                          const nsAtom* aLanguage) const {
  mozilla::Side side;
  if (mTextEmphasisPosition & StyleTextEmphasisPosition::AUTO) {
    // 'auto' resolves to 'under right' for Chinese, 'over right' otherwise.
    if (aWM.IsVertical()) {
      side = eSideRight;
    } else {
      if (nsStyleUtil::MatchesLanguagePrefix(aLanguage, u"zh")) {
        side = eSideBottom;
      } else {
        side = eSideTop;
      }
    }
  } else {
    if (aWM.IsVertical()) {
      side = mTextEmphasisPosition & StyleTextEmphasisPosition::LEFT
                 ? eSideLeft
                 : eSideRight;
    } else {
      side = mTextEmphasisPosition & StyleTextEmphasisPosition::OVER
                 ? eSideTop
                 : eSideBottom;
    }
  }

  LogicalSide result = aWM.LogicalSideForPhysicalSide(side);
  MOZ_ASSERT(IsBlock(result));
  return result;
}

//-----------------------
// nsStyleUI
//

nsStyleUI::nsStyleUI()
    : mInert(StyleInert::None),
      mMozTheme(StyleMozTheme::Auto),
      mUserFocus(StyleUserFocus::Normal),
      mPointerEvents(StylePointerEvents::Auto),
      mCursor{{}, StyleCursorKind::Auto},
      mAccentColor(StyleColorOrAuto::Auto()),
      mCaretColor(StyleColorOrAuto::Auto()),
      mScrollbarColor(StyleScrollbarColor::Auto()),
      mColorScheme(StyleColorScheme{{}, {}}) {
  MOZ_COUNT_CTOR(nsStyleUI);
}

nsStyleUI::nsStyleUI(const nsStyleUI& aSource)
    : mInert(aSource.mInert),
      mMozTheme(aSource.mMozTheme),
      mUserFocus(aSource.mUserFocus),
      mPointerEvents(aSource.mPointerEvents),
      mCursor(aSource.mCursor),
      mAccentColor(aSource.mAccentColor),
      mCaretColor(aSource.mCaretColor),
      mScrollbarColor(aSource.mScrollbarColor),
      mColorScheme(aSource.mColorScheme) {
  MOZ_COUNT_CTOR(nsStyleUI);
}

void nsStyleUI::TriggerImageLoads(Document& aDocument,
                                  const nsStyleUI* aOldStyle) {
  MOZ_ASSERT(NS_IsMainThread());

  auto cursorImages = mCursor.images.AsSpan();
  auto oldCursorImages = aOldStyle ? aOldStyle->mCursor.images.AsSpan()
                                   : Span<const StyleCursorImage>();
  for (size_t i = 0; i < cursorImages.Length(); ++i) {
    const auto& cursor = cursorImages[i];
    const auto* oldCursorImage =
        oldCursorImages.Length() > i ? &oldCursorImages[i].image : nullptr;
    const_cast<StyleCursorImage&>(cursor).image.ResolveImage(aDocument,
                                                             oldCursorImage);
  }
}

nsChangeHint nsStyleUI::CalcDifference(const nsStyleUI& aNewData) const {
  // SVGGeometryFrame's mRect depends on stroke _and_ on the value of
  // pointer-events. See SVGGeometryFrame::ReflowSVG's use of GetHitTestFlags.
  // (Only a reflow, no visual change.)
  //
  // pointer-events changes can change event regions overrides on layers and
  // so needs a repaint.
  const auto kPointerEventsHint =
      nsChangeHint_NeedReflow | nsChangeHint_SchedulePaint;

  nsChangeHint hint = nsChangeHint(0);
  if (mCursor != aNewData.mCursor) {
    hint |= nsChangeHint_UpdateCursor;
  }

  if (mPointerEvents != aNewData.mPointerEvents) {
    hint |= kPointerEventsHint;
  }

  if (mInert != aNewData.mInert) {
    // inert affects pointer-events, user-select, user-focus.
    // Do the union of all them (minus
    // nsChangeHint_NeutralChange which isn't needed if there's any other hint).
    hint |= NS_STYLE_HINT_VISUAL | kPointerEventsHint;
  }

  if (mUserFocus != aNewData.mUserFocus) {
    hint |= nsChangeHint_NeutralChange;
  }

  if (mCaretColor != aNewData.mCaretColor ||
      mAccentColor != aNewData.mAccentColor ||
      mScrollbarColor != aNewData.mScrollbarColor ||
      mMozTheme != aNewData.mMozTheme ||
      mColorScheme != aNewData.mColorScheme) {
    hint |= nsChangeHint_RepaintFrame;
  }

  return hint;
}

//-----------------------
// nsStyleUIReset
//

nsStyleUIReset::nsStyleUIReset()
    : mUserSelect(StyleUserSelect::Auto),
      mScrollbarWidth(StyleScrollbarWidth::Auto),
      mMozForceBrokenImageIcon(false),
      mMozSubtreeHiddenOnlyVisually(false),
      mIMEMode(StyleImeMode::Auto),
      mWindowDragging(StyleWindowDragging::Default),
      mWindowShadow(StyleWindowShadow::Auto),
      mFieldSizing(StyleFieldSizing::Fixed),
      mMozWindowInputRegionMargin(StyleLength::Zero()),
      mTransitions(
          nsStyleAutoArray<StyleTransition>::WITH_SINGLE_INITIAL_ELEMENT),
      mTransitionTimingFunctionCount(1),
      mTransitionDurationCount(1),
      mTransitionDelayCount(1),
      mTransitionPropertyCount(1),
      mTransitionBehaviorCount(1),
      mWindowOpacity(1.0),
      mAnimations(
          nsStyleAutoArray<StyleAnimation>::WITH_SINGLE_INITIAL_ELEMENT),
      mAnimationTimingFunctionCount(1),
      mAnimationDurationCount(1),
      mAnimationDelayCount(1),
      mAnimationNameCount(1),
      mAnimationDirectionCount(1),
      mAnimationFillModeCount(1),
      mAnimationPlayStateCount(1),
      mAnimationIterationCountCount(1),
      mAnimationCompositionCount(1),
      mAnimationTimelineCount(1),
      mAnimationRangeStartCount(1),
      mAnimationRangeEndCount(1),
      mScrollTimelines(
          nsStyleAutoArray<StyleScrollTimeline>::WITH_SINGLE_INITIAL_ELEMENT),
      mScrollTimelineNameCount(1),
      mScrollTimelineAxisCount(1),
      mViewTimelines(
          nsStyleAutoArray<StyleViewTimeline>::WITH_SINGLE_INITIAL_ELEMENT),
      mViewTimelineNameCount(1),
      mViewTimelineAxisCount(1),
      mViewTimelineInsetCount(1),
      mViewTransitionName(StyleAtom{nsGkAtoms::none}) {
  MOZ_COUNT_CTOR(nsStyleUIReset);
}

nsStyleUIReset::nsStyleUIReset(const nsStyleUIReset& aSource)
    : mUserSelect(aSource.mUserSelect),
      mScrollbarWidth(aSource.mScrollbarWidth),
      mMozForceBrokenImageIcon(aSource.mMozForceBrokenImageIcon),
      mMozSubtreeHiddenOnlyVisually(aSource.mMozSubtreeHiddenOnlyVisually),
      mIMEMode(aSource.mIMEMode),
      mWindowDragging(aSource.mWindowDragging),
      mWindowShadow(aSource.mWindowShadow),
      mFieldSizing(aSource.mFieldSizing),
      mMozWindowInputRegionMargin(aSource.mMozWindowInputRegionMargin),
      mMozWindowTransform(aSource.mMozWindowTransform),
      mTransitions(aSource.mTransitions.Clone()),
      mTransitionTimingFunctionCount(aSource.mTransitionTimingFunctionCount),
      mTransitionDurationCount(aSource.mTransitionDurationCount),
      mTransitionDelayCount(aSource.mTransitionDelayCount),
      mTransitionPropertyCount(aSource.mTransitionPropertyCount),
      mTransitionBehaviorCount(aSource.mTransitionBehaviorCount),
      mWindowOpacity(aSource.mWindowOpacity),
      mAnimations(aSource.mAnimations.Clone()),
      mAnimationTimingFunctionCount(aSource.mAnimationTimingFunctionCount),
      mAnimationDurationCount(aSource.mAnimationDurationCount),
      mAnimationDelayCount(aSource.mAnimationDelayCount),
      mAnimationNameCount(aSource.mAnimationNameCount),
      mAnimationDirectionCount(aSource.mAnimationDirectionCount),
      mAnimationFillModeCount(aSource.mAnimationFillModeCount),
      mAnimationPlayStateCount(aSource.mAnimationPlayStateCount),
      mAnimationIterationCountCount(aSource.mAnimationIterationCountCount),
      mAnimationCompositionCount(aSource.mAnimationCompositionCount),
      mAnimationTimelineCount(aSource.mAnimationTimelineCount),
      mAnimationRangeStartCount(aSource.mAnimationRangeStartCount),
      mAnimationRangeEndCount(aSource.mAnimationRangeEndCount),
      mScrollTimelines(aSource.mScrollTimelines.Clone()),
      mScrollTimelineNameCount(aSource.mScrollTimelineNameCount),
      mScrollTimelineAxisCount(aSource.mScrollTimelineAxisCount),
      mViewTimelines(aSource.mViewTimelines.Clone()),
      mViewTimelineNameCount(aSource.mViewTimelineNameCount),
      mViewTimelineAxisCount(aSource.mViewTimelineAxisCount),
      mViewTimelineInsetCount(aSource.mViewTimelineInsetCount),
      mViewTransitionName(aSource.mViewTransitionName),
      mViewTransitionClass(aSource.mViewTransitionClass),
      mTimelineScope(aSource.mTimelineScope) {
  MOZ_COUNT_CTOR(nsStyleUIReset);
}

nsChangeHint nsStyleUIReset::CalcDifference(
    const nsStyleUIReset& aNewData) const {
  nsChangeHint hint = nsChangeHint(0);

  if (mMozForceBrokenImageIcon != aNewData.mMozForceBrokenImageIcon) {
    hint |= nsChangeHint_ReconstructFrame;
  }
  if (mMozSubtreeHiddenOnlyVisually != aNewData.mMozSubtreeHiddenOnlyVisually) {
    hint |= nsChangeHint_RepaintFrame;
  }
  if (mFieldSizing != aNewData.mFieldSizing) {
    hint |= nsChangeHint_AllReflowHints;
  }
  if (mScrollbarWidth != aNewData.mScrollbarWidth) {
    // For scrollbar-width change, we need some special handling similar
    // to overflow properties. Specifically, we may need to reconstruct
    // the scrollbar or force reflow of the viewport scrollbar.
    hint |= nsChangeHint_ScrollbarChange;
  }
  if (mWindowShadow != aNewData.mWindowShadow) {
    // We really need just an nsChangeHint_SyncFrameView, except
    // on an ancestor of the frame, so we get that by doing a
    // reflow.
    hint |= NS_STYLE_HINT_REFLOW;
  }
  if (mUserSelect != aNewData.mUserSelect) {
    hint |= NS_STYLE_HINT_VISUAL;
  }

  if (mWindowDragging != aNewData.mWindowDragging) {
    hint |= nsChangeHint_SchedulePaint;
  }

  if (mViewTransitionName.value != aNewData.mViewTransitionName.value) {
    if (HasViewTransitionName() != aNewData.HasViewTransitionName()) {
      hint |= nsChangeHint_RepaintFrame;
    } else {
      hint |= nsChangeHint_NeutralChange;
    }
  }

  if (mViewTransitionClass.value != aNewData.mViewTransitionClass.value) {
    hint |= nsChangeHint_NeutralChange;
  }

  if (!hint &&
      (mTransitions != aNewData.mTransitions ||
       mTransitionTimingFunctionCount !=
           aNewData.mTransitionTimingFunctionCount ||
       mTransitionDurationCount != aNewData.mTransitionDurationCount ||
       mTransitionDelayCount != aNewData.mTransitionDelayCount ||
       mTransitionPropertyCount != aNewData.mTransitionPropertyCount ||
       mTransitionBehaviorCount != aNewData.mTransitionBehaviorCount ||
       mAnimations != aNewData.mAnimations ||
       mAnimationTimingFunctionCount !=
           aNewData.mAnimationTimingFunctionCount ||
       mAnimationDurationCount != aNewData.mAnimationDurationCount ||
       mAnimationDelayCount != aNewData.mAnimationDelayCount ||
       mAnimationNameCount != aNewData.mAnimationNameCount ||
       mAnimationDirectionCount != aNewData.mAnimationDirectionCount ||
       mAnimationFillModeCount != aNewData.mAnimationFillModeCount ||
       mAnimationPlayStateCount != aNewData.mAnimationPlayStateCount ||
       mAnimationIterationCountCount !=
           aNewData.mAnimationIterationCountCount ||
       mAnimationCompositionCount != aNewData.mAnimationCompositionCount ||
       mAnimationTimelineCount != aNewData.mAnimationTimelineCount ||
       mAnimationRangeStartCount != aNewData.mAnimationRangeStartCount ||
       mAnimationRangeEndCount != aNewData.mAnimationRangeEndCount ||
       mIMEMode != aNewData.mIMEMode ||
       mWindowOpacity != aNewData.mWindowOpacity ||
       mMozWindowInputRegionMargin != aNewData.mMozWindowInputRegionMargin ||
       mMozWindowTransform != aNewData.mMozWindowTransform ||
       mScrollTimelines != aNewData.mScrollTimelines ||
       mScrollTimelineNameCount != aNewData.mScrollTimelineNameCount ||
       mScrollTimelineAxisCount != aNewData.mScrollTimelineAxisCount ||
       mViewTimelines != aNewData.mViewTimelines ||
       mViewTimelineNameCount != aNewData.mViewTimelineNameCount ||
       mViewTimelineAxisCount != aNewData.mViewTimelineAxisCount ||
       mViewTimelineInsetCount != aNewData.mViewTimelineInsetCount ||
       mTimelineScope != aNewData.mTimelineScope)) {
    hint |= nsChangeHint_NeutralChange;
  }

  return hint;
}

//-----------------------
// nsStyleEffects
//

nsStyleEffects::nsStyleEffects()
    : mClip(StyleClipRectOrAuto::Auto()),
      mOpacity(1.0f),
      mMixBlendMode(StyleBlend::Normal) {
  MOZ_COUNT_CTOR(nsStyleEffects);
}

nsStyleEffects::nsStyleEffects(const nsStyleEffects& aSource)
    : mFilters(aSource.mFilters),
      mBoxShadow(aSource.mBoxShadow),
      mBackdropFilters(aSource.mBackdropFilters),
      mClip(aSource.mClip),
      mOpacity(aSource.mOpacity),
      mMixBlendMode(aSource.mMixBlendMode) {
  MOZ_COUNT_CTOR(nsStyleEffects);
}

static bool AnyAutonessChanged(const StyleClipRectOrAuto& aOld,
                               const StyleClipRectOrAuto& aNew) {
  if (aOld.IsAuto() != aNew.IsAuto()) {
    return true;
  }
  if (aOld.IsAuto()) {
    return false;
  }
  const auto& oldRect = aOld.AsRect();
  const auto& newRect = aNew.AsRect();
  return oldRect.top.IsAuto() != newRect.top.IsAuto() ||
         oldRect.right.IsAuto() != newRect.right.IsAuto() ||
         oldRect.bottom.IsAuto() != newRect.bottom.IsAuto() ||
         oldRect.left.IsAuto() != newRect.left.IsAuto();
}

nsChangeHint nsStyleEffects::CalcDifference(
    const nsStyleEffects& aNewData) const {
  nsChangeHint hint = nsChangeHint(0);

  if (mBoxShadow != aNewData.mBoxShadow) {
    // Update overflow regions & trigger DLBI to be sure it's noticed.
    // Also request a repaint, since it's possible that only the color
    // of the shadow is changing (and UpdateOverflow/SchedulePaint won't
    // repaint for that, since they won't know what needs invalidating.)
    hint |= nsChangeHint_UpdateOverflow | nsChangeHint_SchedulePaint |
            nsChangeHint_RepaintFrame;
  }

  if (AnyAutonessChanged(mClip, aNewData.mClip)) {
    hint |= nsChangeHint_AllReflowHints | nsChangeHint_RepaintFrame;
  } else if (mClip != aNewData.mClip) {
    // If the clip has changed, we just need to update overflow areas. DLBI
    // will handle the invalidation.
    hint |= nsChangeHint_UpdateOverflow | nsChangeHint_SchedulePaint;
  }

  if (mOpacity != aNewData.mOpacity) {
    hint |= nsChangeHint_UpdateOpacityLayer;

    // If we're going from the optimized >=0.99 opacity value to 1.0 or back,
    // then repaint the frame because DLBI will not catch the invalidation.
    // Otherwise, just update the opacity layer.
    if ((mOpacity >= 0.99f && mOpacity < 1.0f && aNewData.mOpacity == 1.0f) ||
        (aNewData.mOpacity >= 0.99f && aNewData.mOpacity < 1.0f &&
         mOpacity == 1.0f)) {
      hint |= nsChangeHint_RepaintFrame;
    } else {
      if ((mOpacity == 1.0f) != (aNewData.mOpacity == 1.0f)) {
        hint |= nsChangeHint_UpdateUsesOpacity;
      }
    }
  }

  if (HasFilters() != aNewData.HasFilters()) {
    // A change from/to being a containing block for position:fixed.
    hint |= nsChangeHint_UpdateContainingBlock;
  }

  if (mFilters != aNewData.mFilters) {
    hint |= nsChangeHint_UpdateEffects | nsChangeHint_RepaintFrame |
            nsChangeHint_UpdateOverflow;
  }

  if (mMixBlendMode != aNewData.mMixBlendMode) {
    hint |= nsChangeHint_RepaintFrame;
  }

  if (HasBackdropFilters() != aNewData.HasBackdropFilters()) {
    // A change from/to being a containing block for position:fixed.
    hint |= nsChangeHint_UpdateContainingBlock;
  }

  if (mBackdropFilters != aNewData.mBackdropFilters) {
    hint |= nsChangeHint_UpdateEffects | nsChangeHint_RepaintFrame;
  }

  return hint;
}

static bool TransformOperationHasPercent(const StyleTransformOperation& aOp) {
  switch (aOp.tag) {
    case StyleTransformOperation::Tag::TranslateX:
      return aOp.AsTranslateX().HasPercent();
    case StyleTransformOperation::Tag::TranslateY:
      return aOp.AsTranslateY().HasPercent();
    case StyleTransformOperation::Tag::TranslateZ:
      return false;
    case StyleTransformOperation::Tag::Translate3D: {
      const auto& translate = aOp.AsTranslate3D();
      // NOTE(emilio): z translation is a `<length>`, so can't have percentages.
      return translate._0.HasPercent() || translate._1.HasPercent();
    }
    case StyleTransformOperation::Tag::Translate: {
      const auto& translate = aOp.AsTranslate();
      return translate._0.HasPercent() || translate._1.HasPercent();
    }
    case StyleTransformOperation::Tag::AccumulateMatrix: {
      const auto& accum = aOp.AsAccumulateMatrix();
      return accum.from_list.HasPercent() || accum.to_list.HasPercent();
    }
    case StyleTransformOperation::Tag::InterpolateMatrix: {
      const auto& interpolate = aOp.AsInterpolateMatrix();
      return interpolate.from_list.HasPercent() ||
             interpolate.to_list.HasPercent();
    }
    case StyleTransformOperation::Tag::Perspective:
    case StyleTransformOperation::Tag::RotateX:
    case StyleTransformOperation::Tag::RotateY:
    case StyleTransformOperation::Tag::RotateZ:
    case StyleTransformOperation::Tag::Rotate:
    case StyleTransformOperation::Tag::Rotate3D:
    case StyleTransformOperation::Tag::SkewX:
    case StyleTransformOperation::Tag::SkewY:
    case StyleTransformOperation::Tag::Skew:
    case StyleTransformOperation::Tag::ScaleX:
    case StyleTransformOperation::Tag::ScaleY:
    case StyleTransformOperation::Tag::ScaleZ:
    case StyleTransformOperation::Tag::Scale:
    case StyleTransformOperation::Tag::Scale3D:
    case StyleTransformOperation::Tag::Matrix:
    case StyleTransformOperation::Tag::Matrix3D:
      return false;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown transform operation");
      return false;
  }
}

template <>
bool StyleTransform::HasPercent() const {
  for (const auto& op : Operations()) {
    if (TransformOperationHasPercent(op)) {
      return true;
    }
  }
  return false;
}

template <>
void StyleCalcNode::ScaleLengthsBy(float aScale) {
  auto ScaleNode = [aScale](const StyleCalcNode& aNode) {
    // This const_cast could be removed by generating more mut-casts, if
    // needed.
    const_cast<StyleCalcNode&>(aNode).ScaleLengthsBy(aScale);
  };

  switch (tag) {
    case Tag::Leaf: {
      const auto& leaf = AsLeaf();
      if (leaf.IsLength()) {
        // This const_cast could be removed by generating more mut-casts, if
        // needed.
        const_cast<Length&>(leaf.AsLength()).ScaleBy(aScale);
      }
      break;
    }
    case Tag::Clamp: {
      const auto& clamp = AsClamp();
      ScaleNode(*clamp.min);
      ScaleNode(*clamp.center);
      ScaleNode(*clamp.max);
      break;
    }
    case Tag::Round: {
      const auto& round = AsRound();
      ScaleNode(*round.value);
      ScaleNode(*round.step);
      break;
    }
    case Tag::ModRem: {
      const auto& modRem = AsModRem();
      ScaleNode(*modRem.dividend);
      ScaleNode(*modRem.divisor);
      break;
    }
    case Tag::MinMax: {
      for (const auto& child : AsMinMax()._0.AsSpan()) {
        ScaleNode(child);
      }
      break;
    }
    case Tag::Sum: {
      for (const auto& child : AsSum().AsSpan()) {
        ScaleNode(child);
      }
      break;
    }
    case Tag::Product: {
      for (const auto& child : AsProduct().AsSpan()) {
        ScaleNode(child);
      }
      break;
    }
    case Tag::Negate: {
      const auto& negate = AsNegate();
      ScaleNode(*negate);
      break;
    }
    case Tag::Invert: {
      const auto& invert = AsInvert();
      ScaleNode(*invert);
      break;
    }
    case Tag::Hypot: {
      for (const auto& child : AsHypot().AsSpan()) {
        ScaleNode(child);
      }
      break;
    }
    case Tag::Abs: {
      const auto& abs = AsAbs();
      ScaleNode(*abs);
      break;
    }
    case Tag::Sign: {
      const auto& sign = AsSign();
      ScaleNode(*sign);
      break;
    }
    case Tag::Anchor: {
      MOZ_ASSERT_UNREACHABLE("Unresolved anchor() function");
      break;
    }
    case Tag::AnchorSize: {
      MOZ_ASSERT_UNREACHABLE("Unresolved anchor-size() function");
      break;
    }
  }
}

bool nsStyleDisplay::PrecludesSizeContainmentOrContentVisibilityWithFrame(
    const nsIFrame& aFrame) const {
  // The spec says that in the case of SVG, the contain property only applies
  // to <svg> elements that have an associated CSS layout box.
  // https://drafts.csswg.org/css-contain/#contain-property
  // Internal SVG elements do not use the standard CSS box model, and wouldn't
  // be affected by size containment. By disabling it we prevent them from
  // becoming query containers for size features.
  if (aFrame.HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    return true;
  }

  // Note: The spec for size containment says it should have no effect
  // - on non-atomic, inline-level boxes.
  // - on internal ruby boxes.
  // - if inner display type is table.
  // - on internal table boxes.
  // https://drafts.csswg.org/css-contain/#containment-size
  bool isNonReplacedInline = aFrame.IsLineParticipant() && !aFrame.IsReplaced();
  return isNonReplacedInline || IsInternalRubyDisplayType() ||
         DisplayInside() == mozilla::StyleDisplayInside::Table ||
         IsInnerTableStyle();
}

ContainSizeAxes nsStyleDisplay::GetContainSizeAxes(
    const nsIFrame& aFrame) const {
  // Short circuit for no containment whatsoever
  if (MOZ_LIKELY(!mEffectiveContainment)) {
    return ContainSizeAxes(false, false);
  }

  if (PrecludesSizeContainmentOrContentVisibilityWithFrame(aFrame)) {
    return ContainSizeAxes(false, false);
  }

  // https://drafts.csswg.org/css-contain-2/#content-visibility
  // If this content skips its content via content-visibility, it always has
  // size containment.
  if (MOZ_LIKELY(!(mEffectiveContainment & StyleContain::SIZE)) &&
      MOZ_UNLIKELY(aFrame.HidesContent())) {
    return ContainSizeAxes(true, true);
  }

  return ContainSizeAxes(
      static_cast<bool>(mEffectiveContainment & StyleContain::INLINE_SIZE),
      static_cast<bool>(mEffectiveContainment & StyleContain::BLOCK_SIZE));
}

StyleContentVisibility nsStyleDisplay::ContentVisibility(
    const nsIFrame& aFrame) const {
  if (MOZ_LIKELY(mContentVisibility == StyleContentVisibility::Visible)) {
    return StyleContentVisibility::Visible;
  }
  // content-visibility applies to elements for which size containment applies.
  // https://drafts.csswg.org/css-contain/#content-visibility
  if (PrecludesSizeContainmentOrContentVisibilityWithFrame(aFrame)) {
    return StyleContentVisibility::Visible;
  }
  // If we're in print/print-preview, or being used as an image, we should
  // always treat `auto` as `visible`.
  if (mContentVisibility == StyleContentVisibility::Auto &&
      (aFrame.PresContext()->IsPrintingOrPrintPreview() ||
       aFrame.PresContext()->Document()->IsBeingUsedAsImage())) {
    return StyleContentVisibility::Visible;
  }
  return mContentVisibility;
}

static nscoord Resolve(const StyleContainIntrinsicSize& aSize,
                       nscoord aNoneValue, const nsIFrame& aFrame,
                       LogicalAxis aAxis) {
  if (aSize.IsNone()) {
    return aNoneValue;
  }
  if (aSize.IsLength()) {
    return aSize.AsLength().ToAppUnits();
  }
  MOZ_ASSERT(aSize.HasAuto());
  if (const auto* element = Element::FromNodeOrNull(aFrame.GetContent())) {
    Maybe<float> lastSize = aAxis == LogicalAxis::Block
                                ? element->GetLastRememberedBSize()
                                : element->GetLastRememberedISize();
    if (lastSize && aFrame.HidesContent()) {
      return CSSPixel::ToAppUnits(*lastSize);
    }
  }
  if (aSize.IsAutoNone()) {
    return aNoneValue;
  }
  return aSize.AsAutoLength().ToAppUnits();
}

Maybe<nscoord> ContainSizeAxes::ContainIntrinsicBSize(
    const nsIFrame& aFrame, nscoord aNoneValue) const {
  if (!mBContained) {
    return Nothing();
  }
  const StyleContainIntrinsicSize& bSize =
      aFrame.StylePosition()->ContainIntrinsicBSize(aFrame.GetWritingMode());
  return Some(Resolve(bSize, aNoneValue, aFrame, LogicalAxis::Block));
}

Maybe<nscoord> ContainSizeAxes::ContainIntrinsicISize(
    const nsIFrame& aFrame, nscoord aNoneValue) const {
  if (!mIContained) {
    return Nothing();
  }
  const StyleContainIntrinsicSize& iSize =
      aFrame.StylePosition()->ContainIntrinsicISize(aFrame.GetWritingMode());
  return Some(Resolve(iSize, aNoneValue, aFrame, LogicalAxis::Inline));
}

nsSize ContainSizeAxes::ContainSize(const nsSize& aUncontainedSize,
                                    const nsIFrame& aFrame) const {
  if (!IsAny()) {
    return aUncontainedSize;
  }
  if (aFrame.GetWritingMode().IsVertical()) {
    return nsSize(
        ContainIntrinsicBSize(aFrame).valueOr(aUncontainedSize.Width()),
        ContainIntrinsicISize(aFrame).valueOr(aUncontainedSize.Height()));
  }
  return nsSize(
      ContainIntrinsicISize(aFrame).valueOr(aUncontainedSize.Width()),
      ContainIntrinsicBSize(aFrame).valueOr(aUncontainedSize.Height()));
}

IntrinsicSize ContainSizeAxes::ContainIntrinsicSize(
    const IntrinsicSize& aUncontainedSize, const nsIFrame& aFrame) const {
  if (!IsAny()) {
    return aUncontainedSize;
  }
  IntrinsicSize result(aUncontainedSize);
  const auto wm = aFrame.GetWritingMode();
  if (Maybe<nscoord> containBSize = ContainIntrinsicBSize(aFrame)) {
    result.BSize(wm) = std::move(containBSize);
  }
  if (Maybe<nscoord> containISize = ContainIntrinsicISize(aFrame)) {
    result.ISize(wm) = std::move(containISize);
  }
  return result;
}
