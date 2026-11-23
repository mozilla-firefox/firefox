/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMSVGAngle.h"

#include "SVGAnimatedOrient.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/dom/SVGAngleBinding.h"
#include "mozilla/dom/SVGSVGElement.h"

namespace mozilla::dom {

NS_SVG_VAL_IMPL_CYCLE_COLLECTION_WRAPPERCACHED(DOMSVGAngle, mSVGElement)

DOMSVGAngle::DOMSVGAngle(SVGSVGElement* aSVGElement)
    : mSVGElement(aSVGElement), mType(AngleType::CreatedValue) {
  mVal = new SVGAnimatedOrient();
  mVal->Init();
}

JSObject* DOMSVGAngle::WrapObject(JSContext* aCx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return SVGAngle_Binding::Wrap(aCx, this, aGivenProto);
}

uint16_t DOMSVGAngle::UnitType() const {
  uint16_t unitType;
  if (mType == AngleType::AnimValue) {
    mSVGElement->FlushAnimations();
    unitType = mVal->mAnimValUnit;
  } else {
    unitType = mVal->mBaseValUnit;
  }
  return SVGAnimatedOrient::IsValidUnitType(unitType)
             ? unitType
             : SVGAngle_Binding::SVG_ANGLETYPE_UNKNOWN;
}

float DOMSVGAngle::Value() const {
  if (mType == AngleType::AnimValue) {
    mSVGElement->FlushAnimations();
    return mVal->GetAnimValue();
  }
  return mVal->GetBaseValue();
}

void DOMSVGAngle::SetValue(float aValue, ErrorResult& aRv) {
  if (mType == AngleType::AnimValue) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }
  bool isBaseVal = mType == AngleType::BaseValue;
  SVGContentUtils::NormalizeFloat(aValue);
  mVal->SetBaseValue(aValue, mVal->mBaseValUnit,
                     isBaseVal ? mSVGElement.get() : nullptr, isBaseVal);
}

float DOMSVGAngle::ValueInSpecifiedUnits() const {
  float value =
      (mType == AngleType::AnimValue) ? mVal->mAnimVal : mVal->mBaseVal;
  return std::clamp(value, std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::max());
}

void DOMSVGAngle::SetValueInSpecifiedUnits(float aValue, ErrorResult& aRv) {
  if (mType == AngleType::AnimValue) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }
  SVGContentUtils::NormalizeFloat(aValue);
  if (mType == AngleType::BaseValue) {
    mVal->SetBaseValueInSpecifiedUnits(aValue, mSVGElement);
  } else {
    mVal->mBaseVal = aValue;
  }
}

void DOMSVGAngle::NewValueSpecifiedUnits(uint16_t aUnitType,
                                         float aValueInSpecifiedUnits,
                                         ErrorResult& aRv) {
  if (mType == AngleType::AnimValue) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }
  if (!SVGAnimatedOrient::IsValidUnitType(aUnitType)) {
    aRv.ThrowNotSupportedError("Unknown unit type");
    return;
  }
  SVGContentUtils::NormalizeFloat(aValueInSpecifiedUnits);
  mVal->NewValueSpecifiedUnits(
      aUnitType, aValueInSpecifiedUnits,
      mType == AngleType::BaseValue ? mSVGElement.get() : nullptr);
}

void DOMSVGAngle::ConvertToSpecifiedUnits(uint16_t aUnitType,
                                          ErrorResult& aRv) {
  if (mType == AngleType::AnimValue) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }
  if (!SVGAnimatedOrient::IsValidUnitType(aUnitType)) {
    aRv.ThrowNotSupportedError("Unknown unit type");
    return;
  }
  mVal->ConvertToSpecifiedUnits(
      aUnitType, mType == AngleType::BaseValue ? mSVGElement.get() : nullptr);
}

void DOMSVGAngle::SetValueAsString(const nsAString& aValue, ErrorResult& aRv) {
  if (mType == AngleType::AnimValue) {
    aRv.ThrowNoModificationAllowedError("Animated values cannot be set");
    return;
  }
  bool isBaseVal = mType == AngleType::BaseValue;
  aRv = mVal->SetBaseValueString(
      aValue, isBaseVal ? mSVGElement.get() : nullptr, isBaseVal);
}

void DOMSVGAngle::GetValueAsString(nsAString& aValue) {
  if (mType == AngleType::AnimValue) {
    mSVGElement->FlushAnimations();
    mVal->GetAnimAngleValueString(aValue);
  } else {
    mVal->GetBaseAngleValueString(aValue);
  }
}

}  // namespace mozilla::dom
