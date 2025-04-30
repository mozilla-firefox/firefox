/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGAnimatedLength.h"

#include "mozAutoDocUpdate.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/GeckoBindings.h"
#include "mozilla/Maybe.h"
#include "mozilla/PresShell.h"
#include "mozilla/SMILValue.h"
#include "mozilla/StaticPresData.h"
#include "nsPresContextInlines.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/dom/SVGViewportElement.h"
#include "DOMSVGAnimatedLength.h"
#include "DOMSVGLength.h"
#include "LayoutLogging.h"
#include "nsContentUtils.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsTextFormatter.h"
#include "SVGLengthSMILType.h"
#include "SVGAttrTearoffTable.h"
#include "SVGGeometryProperty.h"

using namespace mozilla::dom;

namespace mozilla {

//----------------------------------------------------------------------
// Helper class: AutoChangeLengthNotifier
// Stack-based helper class to pair calls to WillChangeLength and
// DidChangeLength.
class MOZ_RAII AutoChangeLengthNotifier {
 public:
  AutoChangeLengthNotifier(SVGAnimatedLength* aLength, SVGElement* aSVGElement,
                           bool aDoSetAttr = true)
      : mLength(aLength), mSVGElement(aSVGElement), mDoSetAttr(aDoSetAttr) {
    MOZ_ASSERT(mLength, "Expecting non-null length");
    MOZ_ASSERT(mSVGElement, "Expecting non-null element");

    if (mDoSetAttr) {
      mUpdateBatch.emplace(aSVGElement->GetComposedDoc(), true);
      mEmptyOrOldValue =
          mSVGElement->WillChangeLength(mLength->mAttrEnum, mUpdateBatch.ref());
    }
  }

  ~AutoChangeLengthNotifier() {
    if (mDoSetAttr) {
      mSVGElement->DidChangeLength(mLength->mAttrEnum, mEmptyOrOldValue,
                                   mUpdateBatch.ref());
    }
    if (mLength->mIsAnimated) {
      mSVGElement->AnimationNeedsResample();
    }
  }

 private:
  SVGAnimatedLength* const mLength;
  SVGElement* const mSVGElement;
  Maybe<mozAutoDocUpdate> mUpdateBatch;
  nsAttrValue mEmptyOrOldValue;
  bool mDoSetAttr;
};

MOZ_CONSTINIT static SVGAttrTearoffTable<SVGAnimatedLength,
                                         DOMSVGAnimatedLength>
    sSVGAnimatedLengthTearoffTable;

/* Helper functions */

static void GetValueString(nsAString& aValueAsString, float aValue,
                           uint16_t aUnitType) {
  nsTextFormatter::ssprintf(aValueAsString, u"%g", (double)aValue);

  nsAutoString unitString;
  SVGLength::GetUnitString(unitString, aUnitType);
  aValueAsString.Append(unitString);
}

static bool GetValueFromString(const nsAString& aString, float& aValue,
                               uint16_t* aUnitType) {
  bool success;
  auto token = SVGContentUtils::GetAndEnsureOneToken(aString, success);

  if (!success) {
    return false;
  }

  nsAString::const_iterator iter, end;
  token.BeginReading(iter);
  token.EndReading(end);

  if (!SVGContentUtils::ParseNumber(iter, end, aValue)) {
    return false;
  }
  const nsAString& units = Substring(iter, end);
  *aUnitType = SVGLength::GetUnitTypeForString(units);
  return *aUnitType != SVGLength_Binding::SVG_LENGTHTYPE_UNKNOWN;
}

static float FixAxisLength(float aLength) {
  if (aLength == 0.0f) {
    LAYOUT_WARNING("zero axis length");
    return 1e-20f;
  }
  return aLength;
}

GeckoFontMetrics UserSpaceMetrics::DefaultFontMetrics() {
  return {StyleLength::FromPixels(16.0f),
          StyleLength::FromPixels(-1),
          StyleLength::FromPixels(-1),
          StyleLength::FromPixels(-1),
          StyleLength::FromPixels(-1),
          StyleLength::FromPixels(16.0f),
          0.0f,
          0.0f};
}

GeckoFontMetrics UserSpaceMetrics::GetFontMetrics(const Element* aElement) {
  GeckoFontMetrics metrics = DefaultFontMetrics();
  auto* presContext =
      aElement ? nsContentUtils::GetContextForContent(aElement) : nullptr;
  if (presContext) {
    SVGGeometryProperty::DoForComputedStyle(
        aElement, [&](const ComputedStyle* style) {
          metrics = Gecko_GetFontMetrics(
              presContext, WritingMode(style).IsVertical(), style->StyleFont(),
              style->StyleFont()->mFont.size,
              StyleQueryFontMetricsFlags::USE_USER_FONT_SET |
                  StyleQueryFontMetricsFlags::NEEDS_CH |
                  StyleQueryFontMetricsFlags::NEEDS_IC);
        });
  }
  return metrics;
}

WritingMode UserSpaceMetrics::GetWritingMode(const Element* aElement) {
  WritingMode writingMode;
  SVGGeometryProperty::DoForComputedStyle(
      aElement,
      [&](const ComputedStyle* style) { writingMode = WritingMode(style); });
  return writingMode;
}

float UserSpaceMetrics::GetZoom(const Element* aElement) {
  float zoom = 1.0f;
  SVGGeometryProperty::DoForComputedStyle(
      aElement, [&](const ComputedStyle* style) {
        zoom = style->EffectiveZoom().ToFloat();
      });
  return zoom;
}

float UserSpaceMetrics::GetExLength(Type aType) const {
  return GetFontMetricsForType(aType).mXSize.ToCSSPixels();
}

float UserSpaceMetrics::GetChSize(Type aType) const {
  auto metrics = GetFontMetricsForType(aType);
  if (metrics.mChSize.ToCSSPixels() > 0.0) {
    return metrics.mChSize.ToCSSPixels();
  }
  auto emLength = GetEmLength(aType);
  WritingMode writingMode = GetWritingModeForType(aType);
  return writingMode.IsVertical() && !writingMode.IsSideways()
             ? emLength
             : emLength * 0.5f;
}

float UserSpaceMetrics::GetIcWidth(Type aType) const {
  auto metrics = GetFontMetricsForType(aType);
  if (metrics.mIcWidth.ToCSSPixels() > 0.0) {
    return metrics.mIcWidth.ToCSSPixels();
  }
  return GetEmLength(aType);
}

float UserSpaceMetrics::GetCapHeight(Type aType) const {
  auto metrics = GetFontMetricsForType(aType);
  if (metrics.mCapHeight.ToCSSPixels() > 0.0) {
    return metrics.mCapHeight.ToCSSPixels();
  }
  return GetEmLength(aType);
}

CSSSize UserSpaceMetrics::GetCSSViewportSizeFromContext(
    const nsPresContext* aContext) {
  return CSSPixel::FromAppUnits(aContext->GetSizeForViewportUnits());
}

SVGElementMetrics::SVGElementMetrics(const SVGElement* aSVGElement,
                                     const SVGViewportElement* aCtx)
    : mSVGElement(aSVGElement), mCtx(aCtx) {}

const Element* SVGElementMetrics::GetElementForType(Type aType) const {
  switch (aType) {
    case Type::This:
      return mSVGElement;
    case Type::Root:
      return mSVGElement ? mSVGElement->OwnerDoc()->GetRootElement() : nullptr;
    default:
      MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
      return nullptr;
  }
}

GeckoFontMetrics SVGElementMetrics::GetFontMetricsForType(Type aType) const {
  return GetFontMetrics(GetElementForType(aType));
}

WritingMode SVGElementMetrics::GetWritingModeForType(Type aType) const {
  return GetWritingMode(GetElementForType(aType));
}

float SVGElementMetrics::GetZoom() const {
  return UserSpaceMetrics::GetZoom(mSVGElement);
}

float SVGElementMetrics::GetRootZoom() const {
  return UserSpaceMetrics::GetZoom(
      mSVGElement ? mSVGElement->OwnerDoc()->GetRootElement() : nullptr);
}

float SVGElementMetrics::GetAxisLength(uint8_t aCtxType) const {
  if (!EnsureCtx()) {
    return 1.0f;
  }

  return FixAxisLength(mCtx->GetLength(aCtxType));
}

CSSSize SVGElementMetrics::GetCSSViewportSize() const {
  if (!mSVGElement) {
    return {0.0f, 0.0f};
  }
  nsPresContext* context = nsContentUtils::GetContextForContent(mSVGElement);
  if (!context) {
    return {0.0f, 0.0f};
  }
  return GetCSSViewportSizeFromContext(context);
}

float SVGElementMetrics::GetLineHeight(Type aType) const {
  return SVGContentUtils::GetLineHeight(GetElementForType(aType));
}

bool SVGElementMetrics::EnsureCtx() const {
  if (!mCtx && mSVGElement) {
    mCtx = mSVGElement->GetCtx();
    if (!mCtx && mSVGElement->IsSVGElement(nsGkAtoms::svg)) {
      const auto* e = static_cast<const SVGViewportElement*>(mSVGElement);

      if (!e->IsInner()) {
        // mSVGElement must be the outer svg element
        mCtx = e;
      }
    }
  }
  return mCtx != nullptr;
}

NonSVGFrameUserSpaceMetrics::NonSVGFrameUserSpaceMetrics(nsIFrame* aFrame)
    : mFrame(aFrame) {
  MOZ_ASSERT(mFrame, "Need a frame");
}

float NonSVGFrameUserSpaceMetrics::GetEmLength(Type aType) const {
  switch (aType) {
    case Type::This:
      return SVGContentUtils::GetFontSize(mFrame);
    case Type::Root:
      return SVGContentUtils::GetFontSize(
          mFrame->PresContext()->Document()->GetRootElement());
    default:
      MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
      return 1.0f;
  }
}

GeckoFontMetrics NonSVGFrameUserSpaceMetrics::GetFontMetricsForType(
    Type aType) const {
  switch (aType) {
    case Type::This:
      return Gecko_GetFontMetrics(
          mFrame->PresContext(), mFrame->GetWritingMode().IsVertical(),
          mFrame->StyleFont(), mFrame->StyleFont()->mFont.size,
          StyleQueryFontMetricsFlags::USE_USER_FONT_SET |
              StyleQueryFontMetricsFlags::NEEDS_CH |
              StyleQueryFontMetricsFlags::NEEDS_IC);
    case Type::Root:
      return GetFontMetrics(
          mFrame->PresContext()->Document()->GetRootElement());
    default:
      MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
      return DefaultFontMetrics();
  }
}

WritingMode NonSVGFrameUserSpaceMetrics::GetWritingModeForType(
    Type aType) const {
  switch (aType) {
    case Type::This:
      return mFrame->GetWritingMode();
    case Type::Root:
      return GetWritingMode(
          mFrame->PresContext()->Document()->GetRootElement());
    default:
      MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
      return WritingMode();
  }
}

float NonSVGFrameUserSpaceMetrics::GetZoom() const {
  return mFrame->Style()->EffectiveZoom().ToFloat();
}

float NonSVGFrameUserSpaceMetrics::GetRootZoom() const {
  return mFrame->PresContext()
      ->FrameConstructor()
      ->GetRootElementStyleFrame()
      ->Style()
      ->EffectiveZoom()
      .ToFloat();
}

gfx::Size NonSVGFrameUserSpaceMetrics::GetSize() const {
  return SVGIntegrationUtils::GetSVGCoordContextForNonSVGFrame(mFrame);
}

CSSSize NonSVGFrameUserSpaceMetrics::GetCSSViewportSize() const {
  return GetCSSViewportSizeFromContext(mFrame->PresContext());
}

float NonSVGFrameUserSpaceMetrics::GetLineHeight(Type aType) const {
  auto* context = mFrame->PresContext();
  switch (aType) {
    case Type::This: {
      const auto lineHeightAu = ReflowInput::CalcLineHeight(
          *mFrame->Style(), context, mFrame->GetContent(), NS_UNCONSTRAINEDSIZE,
          1.0f);
      return CSSPixel::FromAppUnits(lineHeightAu);
    }
    case Type::Root:
      return SVGContentUtils::GetLineHeight(
          context->Document()->GetRootElement());
  }
  MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
  return 1.0f;
}

float UserSpaceMetricsWithSize::GetAxisLength(uint8_t aCtxType) const {
  gfx::Size size = GetSize();
  float length;
  switch (aCtxType) {
    case SVGContentUtils::X:
      length = size.width;
      break;
    case SVGContentUtils::Y:
      length = size.height;
      break;
    case SVGContentUtils::XY:
      length =
          SVGContentUtils::ComputeNormalizedHypotenuse(size.width, size.height);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown axis type");
      length = 1;
      break;
  }
  return FixAxisLength(length);
}

float SVGAnimatedLength::GetPixelsPerUnit(const SVGElement* aSVGElement,
                                          uint8_t aUnitType) const {
  return SVGLength::GetPixelsPerUnit(SVGElementMetrics(aSVGElement), aUnitType,
                                     mCtxType, false);
}

float SVGAnimatedLength::GetPixelsPerUnitWithZoom(const SVGElement* aSVGElement,
                                                  uint8_t aUnitType) const {
  return SVGLength::GetPixelsPerUnit(SVGElementMetrics(aSVGElement), aUnitType,
                                     mCtxType, true);
}

float SVGAnimatedLength::GetPixelsPerUnitWithZoom(
    const SVGViewportElement* aCtx, uint8_t aUnitType) const {
  return SVGLength::GetPixelsPerUnit(SVGElementMetrics(aCtx, aCtx), aUnitType,
                                     mCtxType, true);
}

float SVGAnimatedLength::GetPixelsPerUnitWithZoom(nsIFrame* aFrame,
                                                  uint8_t aUnitType) const {
  const nsIContent* content = aFrame->GetContent();
  MOZ_ASSERT(!content->IsText(), "Not expecting text content");
  if (content->IsSVGElement()) {
    return SVGLength::GetPixelsPerUnit(
        SVGElementMetrics(static_cast<const SVGElement*>(content)), aUnitType,
        mCtxType, true);
  }
  return SVGLength::GetPixelsPerUnit(NonSVGFrameUserSpaceMetrics(aFrame),
                                     aUnitType, mCtxType, true);
}

float SVGAnimatedLength::GetPixelsPerUnitWithZoom(
    const UserSpaceMetrics& aMetrics, uint8_t aUnitType) const {
  return SVGLength::GetPixelsPerUnit(aMetrics, aUnitType, mCtxType, true);
}

void SVGAnimatedLength::SetBaseValueInSpecifiedUnits(float aValue,
                                                     SVGElement* aSVGElement,
                                                     bool aDoSetAttr) {
  if (mIsBaseSet && mBaseVal == aValue) {
    return;
  }

  AutoChangeLengthNotifier notifier(this, aSVGElement, aDoSetAttr);

  mBaseVal = aValue;
  mIsBaseSet = true;
  if (!mIsAnimated) {
    mAnimVal = mBaseVal;
  }
}

void SVGAnimatedLength::ConvertToSpecifiedUnits(uint16_t aUnitType,
                                                SVGElement* aSVGElement,
                                                ErrorResult& aRv) {
  MOZ_ASSERT(SVGLength::IsValidUnitType(aUnitType), "Unexpected unit type");

  if (mIsBaseSet && mBaseUnitType == uint8_t(aUnitType)) return;

  float valueInSpecifiedUnits;

  if (SVGLength::IsAbsoluteUnit(aUnitType) &&
      SVGLength::IsAbsoluteUnit(mBaseUnitType)) {
    valueInSpecifiedUnits =
        mBaseVal * SVGLength::GetAbsUnitsPerAbsUnit(aUnitType, mBaseUnitType);
  } else {
    float pixelsPerUnit = GetPixelsPerUnit(aSVGElement, aUnitType);
    float valueInPixels =
        mBaseVal * GetPixelsPerUnit(aSVGElement, mBaseUnitType);
    valueInSpecifiedUnits = valueInPixels / pixelsPerUnit;
  }

  if (!std::isfinite(valueInSpecifiedUnits)) {
    aRv.ThrowTypeError<MSG_NOT_FINITE>("value");
    return;
  }

  // Even though we're not changing the visual effect this length will have
  // on the document, we still need to send out notifications in case we have
  // mutation listeners, since the actual string value of the attribute will
  // change.
  AutoChangeLengthNotifier notifier(this, aSVGElement);

  mBaseUnitType = uint8_t(aUnitType);
  if (!mIsAnimated) {
    mAnimUnitType = mBaseUnitType;
  }
  // Setting aDoSetAttr to false here will ensure we don't call
  // Will/DidChangeAngle a second time (and dispatch duplicate notifications).
  SetBaseValueInSpecifiedUnits(valueInSpecifiedUnits, aSVGElement, false);
}

void SVGAnimatedLength::NewValueSpecifiedUnits(uint16_t aUnitType,
                                               float aValueInSpecifiedUnits,
                                               SVGElement* aSVGElement) {
  MOZ_ASSERT(SVGLength::IsValidUnitType(aUnitType), "Unexpected unit type");

  if (mIsBaseSet && mBaseVal == aValueInSpecifiedUnits &&
      mBaseUnitType == uint8_t(aUnitType)) {
    return;
  }

  AutoChangeLengthNotifier notifier(this, aSVGElement);

  mBaseVal = aValueInSpecifiedUnits;
  mIsBaseSet = true;
  mBaseUnitType = uint8_t(aUnitType);
  if (!mIsAnimated) {
    mAnimVal = mBaseVal;
    mAnimUnitType = mBaseUnitType;
  }
}

already_AddRefed<DOMSVGLength> SVGAnimatedLength::ToDOMBaseVal(
    SVGElement* aSVGElement) {
  return DOMSVGLength::GetTearOff(this, aSVGElement, false);
}

already_AddRefed<DOMSVGLength> SVGAnimatedLength::ToDOMAnimVal(
    SVGElement* aSVGElement) {
  return DOMSVGLength::GetTearOff(this, aSVGElement, true);
}

/* Implementation */

nsresult SVGAnimatedLength::SetBaseValueString(const nsAString& aValueAsString,
                                               SVGElement* aSVGElement,
                                               bool aDoSetAttr) {
  float value;
  uint16_t unitType;

  if (!GetValueFromString(aValueAsString, value, &unitType)) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  if (mIsBaseSet && mBaseVal == float(value) &&
      mBaseUnitType == uint8_t(unitType)) {
    return NS_OK;
  }

  AutoChangeLengthNotifier notifier(this, aSVGElement, aDoSetAttr);

  mBaseVal = value;
  mIsBaseSet = true;
  mBaseUnitType = uint8_t(unitType);
  if (!mIsAnimated) {
    mAnimVal = mBaseVal;
    mAnimUnitType = mBaseUnitType;
  }

  return NS_OK;
}

void SVGAnimatedLength::GetBaseValueString(nsAString& aValueAsString) const {
  GetValueString(aValueAsString, mBaseVal, mBaseUnitType);
}

void SVGAnimatedLength::GetAnimValueString(nsAString& aValueAsString) const {
  GetValueString(aValueAsString, mAnimVal, mAnimUnitType);
}

nsresult SVGAnimatedLength::SetBaseValue(float aValue, SVGElement* aSVGElement,
                                         bool aDoSetAttr) {
  float pixelsPerUnit = GetPixelsPerUnit(aSVGElement, mBaseUnitType);
  if (pixelsPerUnit == 0.0f) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  float valueInSpecifiedUnits = aValue / pixelsPerUnit;
  if (!std::isfinite(valueInSpecifiedUnits)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  SetBaseValueInSpecifiedUnits(valueInSpecifiedUnits, aSVGElement, aDoSetAttr);
  return NS_OK;
}

void SVGAnimatedLength::SetAnimValueInSpecifiedUnits(float aValue,
                                                     SVGElement* aSVGElement) {
  if (mAnimVal == aValue && mIsAnimated) {
    return;
  }
  mAnimVal = aValue;
  mIsAnimated = true;
  aSVGElement->DidAnimateLength(mAttrEnum);
}

void SVGAnimatedLength::SetAnimValue(float aValue, uint16_t aUnitType,
                                     SVGElement* aSVGElement) {
  if (mIsAnimated && mAnimVal == aValue && mAnimUnitType == aUnitType) {
    return;
  }
  mAnimVal = aValue;
  mAnimUnitType = aUnitType;
  mIsAnimated = true;
  aSVGElement->DidAnimateLength(mAttrEnum);
}

already_AddRefed<DOMSVGAnimatedLength> SVGAnimatedLength::ToDOMAnimatedLength(
    SVGElement* aSVGElement) {
  RefPtr<DOMSVGAnimatedLength> svgAnimatedLength =
      sSVGAnimatedLengthTearoffTable.GetTearoff(this);
  if (!svgAnimatedLength) {
    svgAnimatedLength = new DOMSVGAnimatedLength(this, aSVGElement);
    sSVGAnimatedLengthTearoffTable.AddTearoff(this, svgAnimatedLength);
  }

  return svgAnimatedLength.forget();
}

DOMSVGAnimatedLength::~DOMSVGAnimatedLength() {
  sSVGAnimatedLengthTearoffTable.RemoveTearoff(mVal);
}

UniquePtr<SMILAttr> SVGAnimatedLength::ToSMILAttr(SVGElement* aSVGElement) {
  return MakeUnique<SMILLength>(this, aSVGElement);
}

nsresult SVGAnimatedLength::SMILLength::ValueFromString(
    const nsAString& aStr, const SVGAnimationElement* /*aSrcElement*/,
    SMILValue& aValue, bool& aPreventCachingOfSandwich) const {
  float value;
  uint16_t unitType;

  if (!GetValueFromString(aStr, value, &unitType)) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  SMILValue val(SVGLengthSMILType::Singleton());
  SVGLengthAndInfo* lai = static_cast<SVGLengthAndInfo*>(val.mU.mPtr);
  lai->Set(value, unitType, mVal->GetCtxType());
  lai->SetInfo(mSVGElement);
  aValue = std::move(val);
  aPreventCachingOfSandwich = !SVGLength::IsAbsoluteUnit(unitType);

  return NS_OK;
}

SMILValue SVGAnimatedLength::SMILLength::GetBaseValue() const {
  SMILValue val(SVGLengthSMILType::Singleton());
  auto* lai = static_cast<SVGLengthAndInfo*>(val.mU.mPtr);
  lai->CopyBaseFrom(*mVal);
  lai->SetInfo(mSVGElement);
  return val;
}

void SVGAnimatedLength::SMILLength::ClearAnimValue() {
  if (mVal->mIsAnimated) {
    mVal->mIsAnimated = false;
    mVal->mAnimVal = mVal->mBaseVal;
    mVal->mAnimUnitType = mVal->mBaseUnitType;
    mSVGElement->DidAnimateLength(mVal->mAttrEnum);
  }
}

nsresult SVGAnimatedLength::SMILLength::SetAnimValue(const SMILValue& aValue) {
  NS_ASSERTION(aValue.mType == SVGLengthSMILType::Singleton(),
               "Unexpected type to assign animated value");
  if (aValue.mType == SVGLengthSMILType::Singleton()) {
    SVGLengthAndInfo* lai = static_cast<SVGLengthAndInfo*>(aValue.mU.mPtr);
    mVal->SetAnimValue(lai->Value(), lai->UnitType(), mSVGElement);
  }
  return NS_OK;
}

float SVGLengthAndInfo::ConvertUnits(const SVGLengthAndInfo& aTo) const {
  if (aTo.mUnitType == mUnitType) {
    return mValue;
  }
  return SVGLength(mValue, mUnitType)
      .GetValueInSpecifiedUnit(aTo.mUnitType, aTo.Element(), aTo.mCtxType);
}

float SVGLengthAndInfo::ValueInPixels(const UserSpaceMetrics& aMetrics) const {
  return mValue == 0.0f ? 0.0f
                        : mValue * SVGLength::GetPixelsPerUnit(
                                       aMetrics, mUnitType, mCtxType, false);
}

void SVGLengthAndInfo::Add(const SVGLengthAndInfo& aValueToAdd,
                           uint32_t aCount) {
  mElement = aValueToAdd.mElement;

  SVGElementMetrics metrics(Element());

  // We may be dealing with two different length units, so we normalize to
  // pixels for the add:

  float currentLength = ValueInPixels(metrics);
  float lengthToAdd = aValueToAdd.ValueInPixels(metrics) * aCount;

  // And then we give the resulting value the same units as the value
  // that we're animating to/by (i.e. the same as aValueToAdd):
  mUnitType = aValueToAdd.mUnitType;
  mCtxType = aValueToAdd.mCtxType;
  mValue = (currentLength + lengthToAdd) /
           SVGLength::GetPixelsPerUnit(metrics, mUnitType, mCtxType, false);
}

void SVGLengthAndInfo::Interpolate(const SVGLengthAndInfo& aStart,
                                   const SVGLengthAndInfo& aEnd,
                                   double aUnitDistance,
                                   SVGLengthAndInfo& aResult) {
  MOZ_ASSERT(!aStart.mElement || aStart.mElement == aEnd.mElement,
             "Should not be interpolating between different elements");

  float startValue, endValue;
  if (!aStart.mElement || aUnitDistance > 0.5) {
    aResult.mUnitType = aEnd.mUnitType;
    aResult.mCtxType = aEnd.mCtxType;
    startValue = aStart.ConvertUnits(aEnd);
    endValue = aEnd.mValue;
  } else {
    aResult.mUnitType = aStart.mUnitType;
    aResult.mCtxType = aStart.mCtxType;
    startValue = aStart.mValue;
    endValue = aEnd.ConvertUnits(aStart);
  }
  aResult.mElement = aEnd.mElement;
  aResult.mValue = startValue + (endValue - startValue) * aUnitDistance;
}

}  // namespace mozilla
