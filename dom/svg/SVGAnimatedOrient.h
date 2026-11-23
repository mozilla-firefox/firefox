/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_SVGANIMATEDORIENT_H_
#define DOM_SVG_SVGANIMATEDORIENT_H_

#include <memory>

#include "DOMSVGAnimatedEnumeration.h"
#include "SVGAnimatedEnumeration.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/SMILAttr.h"
#include "mozilla/dom/SVGAngleBinding.h"
#include "mozilla/dom/SVGMarkerElementBinding.h"
#include "nsError.h"

class nsISupports;

namespace mozilla {

class SMILValue;

namespace dom {
class DOMSVGAngle;
class DOMSVGAnimatedAngle;
class SVGAnimationElement;
class SVGElement;
}  // namespace dom

class SVGAnimatedOrient {
  friend class AutoChangeOrientNotifier;
  friend class dom::DOMSVGAngle;
  friend class dom::DOMSVGAnimatedAngle;
  using SVGElement = dom::SVGElement;

 public:
  void Init() {
    mAnimVal = mBaseVal = .0f;
    mAnimType = mBaseType =
        dom::SVGMarkerElement_Binding::SVG_MARKER_ORIENT_ANGLE;
    mAnimValUnit = mBaseValUnit =
        dom::SVGAngle_Binding::SVG_ANGLETYPE_UNSPECIFIED;
    mIsAnimated = false;
  }

  nsresult SetBaseValueString(const nsAString& aValue, SVGElement* aSVGElement,
                              bool aDoSetAttr);
  void GetBaseValueString(nsAString& aValue) const;
  void GetBaseAngleValueString(nsAString& aValue) const;
  void GetAnimAngleValueString(nsAString& aValue) const;

  float GetBaseValue() const {
    return std::clamp(mBaseVal * GetDegreesPerUnit(mBaseValUnit),
                      std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::max());
  }
  float GetAnimValue() const {
    return std::clamp(mAnimVal * GetDegreesPerUnit(mAnimValUnit),
                      std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::max());
  }
  SVGEnumValue GetAnimType() const { return mAnimType; }

  void SetBaseValue(float aValue, uint16_t aUnit, SVGElement* aSVGElement,
                    bool aDoSetAttr);
  void SetBaseType(SVGEnumValue aValue, SVGElement* aSVGElement,
                   ErrorResult& aRv);
  void SetAnimValue(float aValue, uint16_t aUnit, SVGElement* aSVGElement);
  void SetAnimType(SVGEnumValue aValue, SVGElement* aSVGElement);

  uint16_t GetBaseValueUnit() const { return mBaseValUnit; }
  uint16_t GetAnimValueUnit() const { return mAnimValUnit; }
  float GetBaseValInSpecifiedUnits() const { return mBaseVal; }
  float GetAnimValInSpecifiedUnits() const { return mAnimVal; }

  static nsresult ToDOMSVGAngle(nsISupports** aResult);
  already_AddRefed<dom::DOMSVGAnimatedAngle> ToDOMAnimatedAngle(
      SVGElement* aSVGElement);
  already_AddRefed<dom::DOMSVGAnimatedEnumeration> ToDOMAnimatedEnum(
      SVGElement* aSVGElement);
  std::unique_ptr<SMILAttr> ToSMILAttr(SVGElement* aSVGElement);

  static bool IsValidUnitType(uint16_t aUnitType);

  static bool GetValueFromString(const nsAString& aString, float& aValue,
                                 uint16_t* aUnitType);
  static float GetDegreesPerUnit(uint16_t aUnit);

 private:
  float mAnimVal;
  float mBaseVal;
  uint8_t mAnimType : 4;
  uint8_t mBaseType : 4;
  uint8_t mAnimValUnit;
  uint8_t mBaseValUnit;
  bool mIsAnimated;

  void SetBaseValueInSpecifiedUnits(float aValue, SVGElement* aSVGElement);
  void NewValueSpecifiedUnits(uint16_t aUnitType, float aValueInSpecifiedUnits,
                              SVGElement* aSVGElement);
  void ConvertToSpecifiedUnits(uint16_t aUnitType, SVGElement* aSVGElement);
  already_AddRefed<dom::DOMSVGAngle> ToDOMBaseVal(SVGElement* aSVGElement);
  already_AddRefed<dom::DOMSVGAngle> ToDOMAnimVal(SVGElement* aSVGElement);

 public:
  // DOM wrapper class for the (DOM)SVGAnimatedEnumeration interface where the
  // wrapped class is SVGAnimatedOrient.
  struct DOMAnimatedEnum final : public dom::DOMSVGAnimatedEnumeration {
    DOMAnimatedEnum(SVGAnimatedOrient* aVal, SVGElement* aSVGElement)
        : DOMSVGAnimatedEnumeration(aSVGElement), mVal(aVal) {}
    ~DOMAnimatedEnum();

    SVGAnimatedOrient* mVal;  // kept alive because it belongs to content

    using dom::DOMSVGAnimatedEnumeration::SetBaseVal;
    uint16_t BaseVal() override { return mVal->mBaseType; }
    void SetBaseVal(uint16_t aBaseVal, ErrorResult& aRv) override {
      mVal->SetBaseType(aBaseVal, mSVGElement, aRv);
    }
    uint16_t AnimVal() override {
      // Script may have modified animation parameters or timeline -- DOM
      // getters need to flush any resample requests to reflect these
      // modifications.
      mSVGElement->FlushAnimations();
      return mVal->mAnimType;
    }
  };

  struct SMILOrient final : public SMILAttr {
   public:
    SMILOrient(SVGAnimatedOrient* aOrient, SVGElement* aSVGElement)
        : mOrient(aOrient), mSVGElement(aSVGElement) {}

    // These will stay alive because a SMILAttr only lives as long
    // as the Compositing step, and DOM elements don't get a chance to
    // die during that.
    SVGAnimatedOrient* mOrient;
    SVGElement* mSVGElement;

    // SMILAttr methods
    nsresult ValueFromString(const nsAString& aStr,
                             const dom::SVGAnimationElement* aSrcElement,
                             SMILValue& aValue,
                             bool& aPreventCachingOfSandwich) const override;
    SMILValue GetBaseValue() const override;
    void ClearAnimValue() override;
    nsresult SetAnimValue(const SMILValue& aValue) override;
  };
};

}  // namespace mozilla

#endif  // DOM_SVG_SVGANIMATEDORIENT_H_
