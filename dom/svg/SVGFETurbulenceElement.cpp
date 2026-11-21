/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGFETurbulenceElement.h"

#include "mozilla/SVGFilterInstance.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/SVGFETurbulenceElementBinding.h"

NS_IMPL_NS_NEW_SVG_ELEMENT(FETurbulence)

using namespace mozilla::gfx;

namespace mozilla::dom {

// Stitch Options
constexpr uint16_t SVG_STITCHTYPE_STITCH = 1;
constexpr uint16_t SVG_STITCHTYPE_NOSTITCH = 2;

static constexpr int32_t MAX_OCTAVES = 10;

JSObject* SVGFETurbulenceElement::WrapNode(JSContext* aCx,
                                           JS::Handle<JSObject*> aGivenProto) {
  return SVGFETurbulenceElement_Binding::Wrap(aCx, this, aGivenProto);
}

SVGElement::NumberInfo SVGFETurbulenceElement::sNumberInfo[1] = {
    {nsGkAtoms::seed, 0}};

SVGElement::NumberPairInfo SVGFETurbulenceElement::sNumberPairInfo[1] = {
    {nsGkAtoms::baseFrequency, 0}};

SVGElement::IntegerInfo SVGFETurbulenceElement::sIntegerInfo[1] = {
    {nsGkAtoms::numOctaves, 1}};

SVGEnumMapping SVGFETurbulenceElement::sTypeMap[] = {
    {nsGkAtoms::fractalNoise, uint8_t(SVGTurbulenceType::FractalNoise)},
    {nsGkAtoms::turbulence, uint8_t(SVGTurbulenceType::Turbulence)},
    {nullptr, 0}};

SVGEnumMapping SVGFETurbulenceElement::sStitchTilesMap[] = {
    {nsGkAtoms::stitch, SVG_STITCHTYPE_STITCH},
    {nsGkAtoms::noStitch, SVG_STITCHTYPE_NOSTITCH},
    {nullptr, 0}};

SVGElement::EnumInfo SVGFETurbulenceElement::sEnumInfo[2] = {
    {nsGkAtoms::type, sTypeMap, uint8_t(SVGTurbulenceType::Turbulence)},
    {nsGkAtoms::stitchTiles, sStitchTilesMap, SVG_STITCHTYPE_NOSTITCH}};

SVGElement::StringInfo SVGFETurbulenceElement::sStringInfo[1] = {
    {nsGkAtoms::result, kNameSpaceID_None, true}};

//----------------------------------------------------------------------
// nsINode methods

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGFETurbulenceElement)

//----------------------------------------------------------------------

already_AddRefed<DOMSVGAnimatedNumber>
SVGFETurbulenceElement::BaseFrequencyX() {
  return mNumberPairAttributes[BASE_FREQ].ToDOMAnimatedNumber(
      SVGAnimatedNumberPairWhichOne::First, this);
}

already_AddRefed<DOMSVGAnimatedNumber>
SVGFETurbulenceElement::BaseFrequencyY() {
  return mNumberPairAttributes[BASE_FREQ].ToDOMAnimatedNumber(
      SVGAnimatedNumberPairWhichOne::Second, this);
}

already_AddRefed<DOMSVGAnimatedInteger> SVGFETurbulenceElement::NumOctaves() {
  return mIntegerAttributes[OCTAVES].ToDOMAnimatedInteger(this);
}

already_AddRefed<DOMSVGAnimatedNumber> SVGFETurbulenceElement::Seed() {
  return mNumberAttributes[SEED].ToDOMAnimatedNumber(this);
}

already_AddRefed<DOMSVGAnimatedEnumeration>
SVGFETurbulenceElement::StitchTiles() {
  return mEnumAttributes[STITCHTILES].ToDOMAnimatedEnum(this);
}

already_AddRefed<DOMSVGAnimatedEnumeration> SVGFETurbulenceElement::Type() {
  return mEnumAttributes[TYPE].ToDOMAnimatedEnum(this);
}

FilterPrimitiveDescription SVGFETurbulenceElement::GetPrimitiveDescription(
    SVGFilterInstance* aInstance, const IntRect& aFilterSubregion,
    const nsTArray<bool>& aInputsAreTainted,
    nsTArray<RefPtr<SourceSurface>>& aInputImages) {
  float fX = mNumberPairAttributes[BASE_FREQ].GetAnimValue(
      SVGAnimatedNumberPairWhichOne::First);
  float fY = mNumberPairAttributes[BASE_FREQ].GetAnimValue(
      SVGAnimatedNumberPairWhichOne::Second);
  float seed = mNumberAttributes[OCTAVES].GetAnimValue();
  int32_t octaves =
      std::min(mIntegerAttributes[OCTAVES].GetAnimValue(), MAX_OCTAVES);
  SVGTurbulenceType type =
      SVGTurbulenceType(mEnumAttributes[TYPE].GetAnimValue());
  uint16_t stitch = mEnumAttributes[STITCHTILES].GetAnimValue();

  if ((fX == 0 && fY == 0) || fX < 0.f || fY < 0.f || octaves < 0) {
    // A base frequency of zero results in transparent black for
    // type="turbulence" and in 50% alpha 50% gray for type="fractalNoise".
    if (type == SVGTurbulenceType::Turbulence) {
      return FilterPrimitiveDescription();
    }
    FloodAttributes atts;
    atts.mColor = sRGBColor(0.5, 0.5, 0.5, 0.5);
    return FilterPrimitiveDescription(AsVariant(std::move(atts)));
  }

  // We interpret the base frequency as relative to user space units. In other
  // words, we consider one turbulence base period to be 1 / fX user space
  // units wide and 1 / fY user space units high. We do not scale the frequency
  // depending on the filter primitive region.
  // We now convert the frequency from user space to filter space.
  // If a frequency in user space units is zero, then it will also be zero in
  // filter space. During the conversion we use a dummy period length of 1
  // for those frequencies but then ignore the converted length and use 0
  // for the converted frequency. This avoids division by zero.
  gfxRect firstPeriodInUserSpace(0, 0, fX == 0 ? 1 : (1 / fX),
                                 fY == 0 ? 1 : (1 / fY));
  gfxRect firstPeriodInFilterSpace =
      aInstance->UserSpaceToFilterSpace(firstPeriodInUserSpace);
  Size frequencyInFilterSpace(
      fX == 0 ? 0 : (1 / firstPeriodInFilterSpace.width),
      fY == 0 ? 0 : (1 / firstPeriodInFilterSpace.height));
  gfxPoint offset = firstPeriodInFilterSpace.TopLeft();

  TurbulenceAttributes atts;
  atts.mOffset = IntPoint::Truncate(offset.x, offset.y);
  atts.mBaseFrequency = frequencyInFilterSpace;
  atts.mSeed = seed;
  atts.mOctaves = octaves;
  atts.mStitchable = stitch == SVG_STITCHTYPE_STITCH;
  atts.mType = type;
  return FilterPrimitiveDescription(AsVariant(std::move(atts)));
}

bool SVGFETurbulenceElement::AttributeAffectsRendering(
    int32_t aNameSpaceID, nsAtom* aAttribute) const {
  return SVGFETurbulenceElementBase::AttributeAffectsRendering(aNameSpaceID,
                                                               aAttribute) ||
         (aNameSpaceID == kNameSpaceID_None &&
          (aAttribute == nsGkAtoms::seed ||
           aAttribute == nsGkAtoms::baseFrequency ||
           aAttribute == nsGkAtoms::numOctaves ||
           aAttribute == nsGkAtoms::type ||
           aAttribute == nsGkAtoms::stitchTiles));
}

nsresult SVGFETurbulenceElement::BindToTree(BindContext& aCtx,
                                            nsINode& aParent) {
  if (aCtx.InComposedDoc()) {
    aCtx.OwnerDoc().SetUseCounter(eUseCounter_custom_feTurbulence);
  }

  return SVGFETurbulenceElementBase::BindToTree(aCtx, aParent);
}

//----------------------------------------------------------------------
// SVGElement methods

SVGElement::NumberAttributesInfo SVGFETurbulenceElement::GetNumberInfo() {
  return NumberAttributesInfo(mNumberAttributes, sNumberInfo,
                              std::size(sNumberInfo));
}

SVGElement::NumberPairAttributesInfo
SVGFETurbulenceElement::GetNumberPairInfo() {
  return NumberPairAttributesInfo(mNumberPairAttributes, sNumberPairInfo,
                                  std::size(sNumberPairInfo));
}

SVGElement::IntegerAttributesInfo SVGFETurbulenceElement::GetIntegerInfo() {
  return IntegerAttributesInfo(mIntegerAttributes, sIntegerInfo,
                               std::size(sIntegerInfo));
}

SVGElement::EnumAttributesInfo SVGFETurbulenceElement::GetEnumInfo() {
  return EnumAttributesInfo(mEnumAttributes, sEnumInfo, std::size(sEnumInfo));
}

SVGElement::StringAttributesInfo SVGFETurbulenceElement::GetStringInfo() {
  return StringAttributesInfo(mStringAttributes, sStringInfo,
                              std::size(sStringInfo));
}

}  // namespace mozilla::dom
