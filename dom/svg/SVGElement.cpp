/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGElement.h"

#include <stdarg.h>

#include "SVGAnimatedBoolean.h"
#include "SVGAnimatedEnumeration.h"
#include "SVGAnimatedInteger.h"
#include "SVGAnimatedIntegerPair.h"
#include "SVGAnimatedLength.h"
#include "SVGAnimatedLengthList.h"
#include "SVGAnimatedNumber.h"
#include "SVGAnimatedNumberList.h"
#include "SVGAnimatedNumberPair.h"
#include "SVGAnimatedOrient.h"
#include "SVGAnimatedPathSegList.h"
#include "SVGAnimatedPointList.h"
#include "SVGAnimatedString.h"
#include "SVGAnimatedTransformList.h"
#include "SVGAnimatedViewBox.h"
#include "SVGGeometryProperty.h"
#include "SVGMotionSMILAttr.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/SVGElementBinding.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "mozilla/dom/SVGTests.h"
#include "mozilla/dom/SVGUnitTypesBinding.h"
#include "nsAttrValueOrString.h"
#include "nsCSSProps.h"
#include "nsCSSValue.h"
#include "nsContentUtils.h"
#include "nsDOMCSSAttrDeclaration.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsIContentInlines.h"
#include "nsIFrame.h"
#include "nsIMutationObserver.h"
#include "nsLayoutUtils.h"
#include "nsQueryObject.h"

// This is needed to ensure correct handling of calls to the
// vararg-list methods in this file:
//   SVGElement::GetAnimated{Length,Number,Integer}Values
// See bug 547964 for details:
static_assert(sizeof(void*) == sizeof(nullptr),
              "nullptr should be the correct size");

nsresult NS_NewSVGElement(mozilla::dom::Element** aResult,
                          already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);
  auto* nim = nodeInfo->NodeInfoManager();
  RefPtr<mozilla::dom::SVGElement> it =
      new (nim) mozilla::dom::SVGElement(nodeInfo.forget());
  nsresult rv = it->Init();

  if (NS_FAILED(rv)) {
    return rv;
  }

  it.forget(aResult);
  return rv;
}

namespace mozilla::dom {
using namespace SVGUnitTypes_Binding;

NS_IMPL_ELEMENT_CLONE_WITH_INIT(SVGElement)

// Use the CC variant of this, even though this class does not define
// a new CC participant, to make QIing to the CC interfaces faster.
NS_IMPL_QUERY_INTERFACE_CYCLE_COLLECTION_INHERITED(SVGElement, SVGElementBase,
                                                   SVGElement)

SVGEnumMapping SVGElement::sSVGUnitTypesMap[] = {
    {nsGkAtoms::userSpaceOnUse, SVG_UNIT_TYPE_USERSPACEONUSE},
    {nsGkAtoms::objectBoundingBox, SVG_UNIT_TYPE_OBJECTBOUNDINGBOX},
    {nullptr, 0}};

SVGElement::SVGElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : SVGElementBase(std::move(aNodeInfo)) {}

SVGElement::~SVGElement() = default;

JSObject* SVGElement::WrapNode(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) {
  return SVGElement_Binding::Wrap(aCx, this, aGivenProto);
}

template <typename Value, typename Info>
void SVGElement::AttributesInfo<Value, Info>::ResetAll() {
  for (uint32_t i = 0; i < mCount; ++i) {
    Reset(i);
  }
}

template <typename Value, typename Info>
void SVGElement::AttributesInfo<Value, Info>::CopyAllFrom(
    const AttributesInfo& aOther) {
  MOZ_DIAGNOSTIC_ASSERT(mCount == aOther.mCount,
                        "Should only be called on clones");
  for (uint32_t i = 0; i < mCount; ++i) {
    mValues[i] = aOther.mValues[i];
  }
}

template <>
void SVGElement::LengthAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Init(mInfos[aAttrEnum].mAxis, aAttrEnum,
                          mInfos[aAttrEnum].mDefaultValue,
                          mInfos[aAttrEnum].mDefaultUnitType);
}

template <>
void SVGElement::LengthListAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].ClearBaseValue(aAttrEnum);
  // caller notifies
}

template <>
void SVGElement::NumberListAttributesInfo::Reset(uint8_t aAttrEnum) {
  MOZ_ASSERT(aAttrEnum < mCount, "Bad attr enum");
  mValues[aAttrEnum].ClearBaseValue(aAttrEnum);
  // caller notifies
}

template <>
void SVGElement::NumberAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Init(aAttrEnum, mInfos[aAttrEnum].mDefaultValue);
}

template <>
void SVGElement::NumberPairAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Init(aAttrEnum, mInfos[aAttrEnum].mDefaultValue);
}

template <>
void SVGElement::IntegerAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Init(aAttrEnum, mInfos[aAttrEnum].mDefaultValue);
}

template <>
void SVGElement::IntegerPairAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Init(aAttrEnum, mInfos[aAttrEnum].mDefaultValue);
}

template <>
void SVGElement::BooleanAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Init(aAttrEnum, mInfos[aAttrEnum].mDefaultValue);
}

template <>
void SVGElement::StringListAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Clear();
  // caller notifies
}

template <>
void SVGElement::EnumAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Init(aAttrEnum, mInfos[aAttrEnum].mDefaultValue);
}

template <>
void SVGElement::StringAttributesInfo::Reset(uint8_t aAttrEnum) {
  mValues[aAttrEnum].Init(aAttrEnum);
}

nsresult SVGElement::CopyInnerTo(mozilla::dom::Element* aDest) {
  nsresult rv = Element::CopyInnerTo(aDest);
  NS_ENSURE_SUCCESS(rv, rv);

  auto* dest = static_cast<SVGElement*>(aDest);

  // cloning a node must retain its internal nonce slot
  if (auto* nonce = static_cast<nsString*>(GetProperty(nsGkAtoms::nonce))) {
    dest->SetNonce(*nonce);
  }

  // If our destination is a print document, copy all the relevant length values
  // etc so that they match the state of the original node.
  if (aDest->OwnerDoc()->IsStaticDocument() ||
      aDest->OwnerDoc()->CloningForSVGUse()) {
    LengthAttributesInfo lengthInfo = GetLengthInfo();
    dest->GetLengthInfo().CopyAllFrom(lengthInfo);
    if (SVGGeometryProperty::ElementMapsLengthsToStyle(this)) {
      for (uint32_t i = 0; i < lengthInfo.mCount; i++) {
        NonCustomCSSPropertyId propId =
            SVGGeometryProperty::AttrEnumToCSSPropId(this, i);

        // We don't map use element width/height currently. We can remove this
        // test when we do.
        if (propId != eCSSProperty_UNKNOWN &&
            lengthInfo.mValues[i].IsAnimated()) {
          dest->SMILOverrideStyle()->SetSMILValue(propId,
                                                  lengthInfo.mValues[i]);
        }
      }
    }
    dest->GetNumberInfo().CopyAllFrom(GetNumberInfo());
    dest->GetNumberPairInfo().CopyAllFrom(GetNumberPairInfo());
    dest->GetIntegerInfo().CopyAllFrom(GetIntegerInfo());
    dest->GetIntegerPairInfo().CopyAllFrom(GetIntegerPairInfo());
    dest->GetBooleanInfo().CopyAllFrom(GetBooleanInfo());
    if (const auto* orient = GetAnimatedOrient()) {
      *dest->GetAnimatedOrient() = *orient;
    }
    if (const auto* viewBox = GetAnimatedViewBox()) {
      *dest->GetAnimatedViewBox() = *viewBox;
    }
    if (const auto* preserveAspectRatio = GetAnimatedPreserveAspectRatio()) {
      *dest->GetAnimatedPreserveAspectRatio() = *preserveAspectRatio;
    }
    dest->GetEnumInfo().CopyAllFrom(GetEnumInfo());
    dest->GetStringInfo().CopyAllFrom(GetStringInfo());
    dest->GetLengthListInfo().CopyAllFrom(GetLengthListInfo());
    dest->GetNumberListInfo().CopyAllFrom(GetNumberListInfo());
    if (const auto* pointList = GetAnimatedPointList()) {
      *dest->GetAnimatedPointList() = *pointList;
    }
    if (const auto* pathSegList = GetAnimatedPathSegList()) {
      *dest->GetAnimatedPathSegList() = *pathSegList;
      if (pathSegList->IsAnimating()) {
        dest->SMILOverrideStyle()->SetSMILValue(eCSSProperty_d, *pathSegList);
      }
    }
    if (const auto* transformList = GetExistingAnimatedTransformList()) {
      *dest->GetOrCreateAnimatedTransformList() = *transformList;
    }
    if (const auto* animateMotionTransform = GetAnimateMotionTransform()) {
      dest->SetAnimateMotionTransform(animateMotionTransform);
    }
    if (const auto* smilOverrideStyleDecoration =
            GetSMILOverrideStyleDeclaration()) {
      RefPtr<StyleLockedDeclarationBlock> declClone =
          Servo_DeclarationBlock_Clone(smilOverrideStyleDecoration).Consume();
      dest->SetSMILOverrideStyleDeclaration(*declClone);
    }
  }

  return NS_OK;
}

//----------------------------------------------------------------------
// SVGElement methods

void SVGElement::WillAnimateClass() {
  if (auto* doc = GetComposedDoc()) {
    if (auto* pc = doc->GetPresContext()) {
      pc->RestyleManager()->ClassAttributeWillBeChangedBySMIL(this);
    }
  }
}

void SVGElement::DidAnimateClass() {
  if (mClassAttribute.IsAnimated()) {
    nsAutoString src;
    mClassAttribute.GetAnimValue(src, this);
    if (!mClassAnimAttr) {
      mClassAnimAttr = std::make_unique<nsAttrValue>();
    }
    mClassAnimAttr->ParseAtomArray(src);
  } else {
    mClassAnimAttr.reset();
  }

  // Update bloom filter after mutating mClassAnimAttr.
  UpdateSubtreeBloomFilterForClass(GetClasses());
  UpdateSubtreeBloomFilterForAttribute(nsGkAtoms::_class);
  PropagateBloomFilterToParents();

  DidAnimateAttribute(kNameSpaceID_None, nsGkAtoms::_class);
}

nsresult SVGElement::Init() {
  // Set up length attributes - can't do this in the constructor
  // because we can't do a virtual call at that point

  GetLengthInfo().ResetAll();
  GetNumberInfo().ResetAll();
  GetNumberPairInfo().ResetAll();
  GetIntegerInfo().ResetAll();
  GetIntegerPairInfo().ResetAll();
  GetBooleanInfo().ResetAll();
  GetEnumInfo().ResetAll();

  if (SVGAnimatedOrient* orient = GetAnimatedOrient()) {
    orient->Init();
  }

  if (SVGAnimatedViewBox* viewBox = GetAnimatedViewBox()) {
    viewBox->Init();
  }

  if (SVGAnimatedPreserveAspectRatio* preserveAspectRatio =
          GetAnimatedPreserveAspectRatio()) {
    preserveAspectRatio->Init();
  }

  GetLengthListInfo().ResetAll();
  GetNumberListInfo().ResetAll();

  // No need to reset SVGPointList since the default value is always the same
  // (an empty list).

  // No need to reset SVGPathData since the default value is always the same
  // (an empty list).

  GetStringInfo().ResetAll();
  return NS_OK;
}

//----------------------------------------------------------------------
// Implementation

//----------------------------------------------------------------------
// nsIContent methods

nsresult SVGElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = SVGElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  // Hide any nonce from the DOM, but keep the internal value of the
  // nonce by copying and resetting the internal nonce value.
  if (!aContext.IsMove() && HasFlag(NODE_HAS_NONCE_AND_HEADER_CSP) &&
      IsInComposedDoc() && OwnerDoc()->GetBrowsingContext()) {
    nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
        "SVGElement::ResetNonce::Runnable",
        [self = RefPtr<SVGElement>(this)]() {
          nsAutoString nonce;
          self->GetNonce(nonce);
          self->SetAttr(kNameSpaceID_None, nsGkAtoms::nonce, u""_ns, true,
                        IsKnownNewAttr::No);
          self->SetNonce(nonce);
        }));
  }

  return NS_OK;
}

void SVGElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                              const nsAttrValue* aValue,
                              const nsAttrValue* aOldValue,
                              nsIPrincipal* aSubjectPrincipal, bool aNotify) {
  if (IsEventAttributeName(aName) && aValue) {
    MOZ_ASSERT(aValue->Type() == nsAttrValue::eString ||
                   aValue->Type() == nsAttrValue::eAtom,
               "Expected string or atom value for script body");
    SetEventHandler(GetEventNameForAttr(aName),
                    nsAttrValueOrString(aValue).String());
  }

  // The nonce will be copied over to an internal slot and cleared from the
  // Element within BindToTree to avoid CSS Selector nonce exfiltration if
  // the CSP list contains a header-delivered CSP.
  if (nsGkAtoms::nonce == aName && kNameSpaceID_None == aNamespaceID) {
    if (aValue) {
      SetNonce(nsAttrValueOrString(aValue).String());
      if (OwnerDoc()->GetHasCSPDeliveredThroughHeader()) {
        SetFlags(NODE_HAS_NONCE_AND_HEADER_CSP);
      }
    } else {
      RemoveNonce();
    }
  }

  return SVGElementBase::AfterSetAttr(aNamespaceID, aName, aValue, aOldValue,
                                      aSubjectPrincipal, aNotify);
}

bool SVGElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                const nsAString& aValue,
                                nsIPrincipal* aMaybeScriptedPrincipal,
                                nsAttrValue& aResult) {
  nsresult rv = NS_OK;
  bool foundMatch = false;
  bool didSetResult = false;

  if (aNamespaceID == kNameSpaceID_None) {
    // Check for SVGAnimatedLength attribute
    LengthAttributesInfo lengthInfo = GetLengthInfo();

    uint32_t i;
    for (i = 0; i < lengthInfo.mCount; i++) {
      if (aAttribute == lengthInfo.mInfos[i].mName) {
        rv = lengthInfo.mValues[i].SetBaseValueString(aValue, this, false);
        if (NS_FAILED(rv)) {
          lengthInfo.Reset(i);
        } else {
          aResult.SetTo(lengthInfo.mValues[i], &aValue);
          didSetResult = true;
        }
        foundMatch = true;
        break;
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedLengthList attribute
      LengthListAttributesInfo lengthListInfo = GetLengthListInfo();
      for (i = 0; i < lengthListInfo.mCount; i++) {
        if (aAttribute == lengthListInfo.mInfos[i].mName) {
          rv = lengthListInfo.mValues[i].SetBaseValueString(aValue);
          if (NS_FAILED(rv)) {
            lengthListInfo.Reset(i);
          } else {
            aResult.SetTo(lengthListInfo.mValues[i].GetBaseValue(), &aValue);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedNumberList attribute
      NumberListAttributesInfo numberListInfo = GetNumberListInfo();
      for (i = 0; i < numberListInfo.mCount; i++) {
        if (aAttribute == numberListInfo.mInfos[i].mName) {
          rv = numberListInfo.mValues[i].SetBaseValueString(aValue);
          if (NS_FAILED(rv)) {
            numberListInfo.Reset(i);
          } else {
            aResult.SetTo(numberListInfo.mValues[i].GetBaseValue(), &aValue);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedPointList attribute
      if (GetPointListAttrName() == aAttribute) {
        if (SVGAnimatedPointList* pointList = GetAnimatedPointList()) {
          rv = pointList->SetBaseValueString(aValue);
          if (NS_FAILED(rv)) {
            pointList->ClearBaseValue();
          } else {
            aResult.SetTo(pointList->GetBaseValue(), &aValue);
            didSetResult = true;
          }
          foundMatch = true;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedPathSegList attribute
      if (GetPathDataAttrName() == aAttribute) {
        if (SVGAnimatedPathSegList* segList = GetAnimatedPathSegList()) {
          segList->SetBaseValueString(aValue);
          // The spec says we parse everything up to the failure, so we DON'T
          // need to check the result of SetBaseValueString or call
          // segList->ClearBaseValue() if it fails
          aResult.SetTo(segList->GetBaseValue(), &aValue);
          didSetResult = true;
          foundMatch = true;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedNumber attribute
      NumberAttributesInfo numberInfo = GetNumberInfo();
      for (i = 0; i < numberInfo.mCount; i++) {
        if (aAttribute == numberInfo.mInfos[i].mName) {
          rv = numberInfo.mValues[i].SetBaseValueString(aValue, this);
          if (NS_FAILED(rv)) {
            numberInfo.Reset(i);
          } else {
            aResult.SetTo(numberInfo.mValues[i].GetBaseValue(), &aValue);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedNumberPair attribute
      NumberPairAttributesInfo numberPairInfo = GetNumberPairInfo();
      for (i = 0; i < numberPairInfo.mCount; i++) {
        if (aAttribute == numberPairInfo.mInfos[i].mName) {
          rv = numberPairInfo.mValues[i].SetBaseValueString(aValue, this);
          if (NS_FAILED(rv)) {
            numberPairInfo.Reset(i);
          } else {
            aResult.SetTo(numberPairInfo.mValues[i], &aValue);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedInteger attribute
      IntegerAttributesInfo integerInfo = GetIntegerInfo();
      for (i = 0; i < integerInfo.mCount; i++) {
        if (aAttribute == integerInfo.mInfos[i].mName) {
          rv = integerInfo.mValues[i].SetBaseValueString(aValue, this);
          if (NS_FAILED(rv)) {
            integerInfo.Reset(i);
          } else {
            aResult.SetTo(integerInfo.mValues[i].GetBaseValue(), &aValue);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedIntegerPair attribute
      IntegerPairAttributesInfo integerPairInfo = GetIntegerPairInfo();
      for (i = 0; i < integerPairInfo.mCount; i++) {
        if (aAttribute == integerPairInfo.mInfos[i].mName) {
          rv = integerPairInfo.mValues[i].SetBaseValueString(aValue, this);
          if (NS_FAILED(rv)) {
            integerPairInfo.Reset(i);
          } else {
            aResult.SetTo(integerPairInfo.mValues[i], &aValue);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedBoolean attribute
      BooleanAttributesInfo booleanInfo = GetBooleanInfo();
      for (i = 0; i < booleanInfo.mCount; i++) {
        if (aAttribute == booleanInfo.mInfos[i].mName) {
          nsAtom* valAtom = NS_GetStaticAtom(aValue);
          rv = valAtom ? booleanInfo.mValues[i].SetBaseValueAtom(valAtom, this)
                       : NS_ERROR_DOM_SYNTAX_ERR;
          if (NS_FAILED(rv)) {
            booleanInfo.Reset(i);
          } else {
            aResult.SetTo(valAtom);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for SVGAnimatedEnumeration attribute
      EnumAttributesInfo enumInfo = GetEnumInfo();
      for (i = 0; i < enumInfo.mCount; i++) {
        if (aAttribute == enumInfo.mInfos[i].mName) {
          RefPtr<nsAtom> valAtom = NS_Atomize(aValue);
          if (!enumInfo.mValues[i].SetBaseValueAtom(valAtom, this)) {
            // Exact error value does not matter; we just need to mark the
            // parse as failed.
            rv = NS_ERROR_FAILURE;
            enumInfo.Reset(i);
          } else {
            aResult.SetTo(valAtom);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for conditional processing attributes
      nsCOMPtr<SVGTests> tests = do_QueryObject(this);
      if (tests && tests->ParseConditionalProcessingAttribute(
                       aAttribute, aValue, aResult)) {
        foundMatch = true;
      }
    }

    if (!foundMatch) {
      // Check for StringList attribute
      StringListAttributesInfo stringListInfo = GetStringListInfo();
      for (i = 0; i < stringListInfo.mCount; i++) {
        if (aAttribute == stringListInfo.mInfos[i].mName) {
          rv = stringListInfo.mValues[i].SetValue(aValue);
          if (NS_FAILED(rv)) {
            stringListInfo.Reset(i);
          } else {
            aResult.SetTo(stringListInfo.mValues[i], &aValue);
            didSetResult = true;
          }
          foundMatch = true;
          break;
        }
      }
    }

    if (!foundMatch) {
      // Check for orient attribute
      if (aAttribute == nsGkAtoms::orient) {
        SVGAnimatedOrient* orient = GetAnimatedOrient();
        if (orient) {
          rv = orient->SetBaseValueString(aValue, this, false);
          if (NS_FAILED(rv)) {
            orient->Init();
          } else {
            aResult.SetTo(*orient, &aValue);
            didSetResult = true;
          }
          foundMatch = true;
        }
        // Check for viewBox attribute
      } else if (aAttribute == nsGkAtoms::viewBox) {
        SVGAnimatedViewBox* viewBox = GetAnimatedViewBox();
        if (viewBox) {
          rv = viewBox->SetBaseValueString(aValue, this, false);
          if (NS_FAILED(rv)) {
            viewBox->Init();
          } else {
            aResult.SetTo(*viewBox, &aValue);
            didSetResult = true;
          }
          foundMatch = true;
        }
        // Check for preserveAspectRatio attribute
      } else if (aAttribute == nsGkAtoms::preserveAspectRatio) {
        SVGAnimatedPreserveAspectRatio* preserveAspectRatio =
            GetAnimatedPreserveAspectRatio();
        if (preserveAspectRatio) {
          rv = preserveAspectRatio->SetBaseValueString(aValue, this, false);
          if (NS_FAILED(rv)) {
            preserveAspectRatio->Init();
          } else {
            aResult.SetTo(*preserveAspectRatio, &aValue);
            didSetResult = true;
          }
          foundMatch = true;
        }
        // Check for SVGAnimatedTransformList attribute
      } else if (GetTransformListAttrName() == aAttribute) {
        // The transform attribute is being set, so we must ensure that the
        // SVGAnimatedTransformList is/has been allocated:
        SVGAnimatedTransformList* transformList =
            GetOrCreateAnimatedTransformList();
        rv = transformList->SetBaseValueString(aValue, this);
        if (NS_FAILED(rv)) {
          transformList->ClearBaseValue();
        } else {
          aResult.SetTo(transformList->GetBaseValue(), &aValue);
          didSetResult = true;
        }
        foundMatch = true;
      } else if (aAttribute == nsGkAtoms::tabindex) {
        didSetResult = aResult.ParseIntValue(aValue);
        foundMatch = true;
      }
    }

    if (aAttribute == nsGkAtoms::_class) {
      mClassAttribute.SetBaseValue(aValue, this, false);
      aResult.ParseAtomArray(aValue);
      return true;
    }

    if (aAttribute == nsGkAtoms::rel) {
      aResult.ParseAtomArray(aValue);
      return true;
    }
  }

  if (!foundMatch) {
    // Check for SVGAnimatedString attribute
    StringAttributesInfo stringInfo = GetStringInfo();
    for (uint32_t i = 0; i < stringInfo.mCount; i++) {
      if (aNamespaceID == stringInfo.mInfos[i].mNamespaceID &&
          aAttribute == stringInfo.mInfos[i].mName) {
        stringInfo.mValues[i].SetBaseValue(aValue, this, false);
        foundMatch = true;
        break;
      }
    }
  }

  if (foundMatch) {
    if (NS_FAILED(rv)) {
      ReportAttributeParseFailure(OwnerDoc(), aAttribute, aValue);
      return false;
    }
    if (!didSetResult) {
      aResult.SetTo(aValue);
    }
    return true;
  }

  return SVGElementBase::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                        aMaybeScriptedPrincipal, aResult);
}

void SVGElement::UnsetAttrInternal(int32_t aNamespaceID, nsAtom* aName,
                                   bool aNotify) {
  // XXXbz there's a bunch of redundancy here with AfterSetAttr.
  // Maybe consolidate?

  if (aNamespaceID == kNameSpaceID_None) {
    if (IsEventAttributeName(aName)) {
      EventListenerManager* manager = GetExistingListenerManager();
      if (manager) {
        nsAtom* eventName = GetEventNameForAttr(aName);
        manager->RemoveEventHandler(eventName);
      }
      return;
    }

    // Check if this is a length attribute going away
    LengthAttributesInfo lenInfo = GetLengthInfo();

    for (uint32_t i = 0; i < lenInfo.mCount; i++) {
      if (aName == lenInfo.mInfos[i].mName) {
        lenInfo.Reset(i);
        return;
      }
    }

    // Check if this is a length list attribute going away
    LengthListAttributesInfo lengthListInfo = GetLengthListInfo();

    for (uint32_t i = 0; i < lengthListInfo.mCount; i++) {
      if (aName == lengthListInfo.mInfos[i].mName) {
        lengthListInfo.Reset(i);
        return;
      }
    }

    // Check if this is a number list attribute going away
    NumberListAttributesInfo numberListInfo = GetNumberListInfo();

    for (uint32_t i = 0; i < numberListInfo.mCount; i++) {
      if (aName == numberListInfo.mInfos[i].mName) {
        numberListInfo.Reset(i);
        return;
      }
    }

    // Check if this is a point list attribute going away
    if (GetPointListAttrName() == aName) {
      SVGAnimatedPointList* pointList = GetAnimatedPointList();
      if (pointList) {
        pointList->ClearBaseValue();
        return;
      }
    }

    // Check if this is a path segment list attribute going away
    if (GetPathDataAttrName() == aName) {
      SVGAnimatedPathSegList* segList = GetAnimatedPathSegList();
      if (segList) {
        segList->ClearBaseValue();
        return;
      }
    }

    // Check if this is a number attribute going away
    NumberAttributesInfo numInfo = GetNumberInfo();

    for (uint32_t i = 0; i < numInfo.mCount; i++) {
      if (aName == numInfo.mInfos[i].mName) {
        numInfo.Reset(i);
        return;
      }
    }

    // Check if this is a number pair attribute going away
    NumberPairAttributesInfo numPairInfo = GetNumberPairInfo();

    for (uint32_t i = 0; i < numPairInfo.mCount; i++) {
      if (aName == numPairInfo.mInfos[i].mName) {
        numPairInfo.Reset(i);
        return;
      }
    }

    // Check if this is an integer attribute going away
    IntegerAttributesInfo intInfo = GetIntegerInfo();

    for (uint32_t i = 0; i < intInfo.mCount; i++) {
      if (aName == intInfo.mInfos[i].mName) {
        intInfo.Reset(i);
        return;
      }
    }

    // Check if this is an integer pair attribute going away
    IntegerPairAttributesInfo intPairInfo = GetIntegerPairInfo();

    for (uint32_t i = 0; i < intPairInfo.mCount; i++) {
      if (aName == intPairInfo.mInfos[i].mName) {
        intPairInfo.Reset(i);
        return;
      }
    }

    // Check if this is a boolean attribute going away
    BooleanAttributesInfo boolInfo = GetBooleanInfo();

    for (uint32_t i = 0; i < boolInfo.mCount; i++) {
      if (aName == boolInfo.mInfos[i].mName) {
        boolInfo.Reset(i);
        return;
      }
    }

    // Check if this is an enum attribute going away
    EnumAttributesInfo enumInfo = GetEnumInfo();

    for (uint32_t i = 0; i < enumInfo.mCount; i++) {
      if (aName == enumInfo.mInfos[i].mName) {
        enumInfo.Reset(i);
        return;
      }
    }

    // Check if this is an orient attribute going away
    if (aName == nsGkAtoms::orient) {
      SVGAnimatedOrient* orient = GetAnimatedOrient();
      if (orient) {
        orient->Init();
        return;
      }
    }

    // Check if this is a viewBox attribute going away
    if (aName == nsGkAtoms::viewBox) {
      SVGAnimatedViewBox* viewBox = GetAnimatedViewBox();
      if (viewBox) {
        viewBox->Init();
        return;
      }
    }

    // Check if this is a preserveAspectRatio attribute going away
    if (aName == nsGkAtoms::preserveAspectRatio) {
      SVGAnimatedPreserveAspectRatio* preserveAspectRatio =
          GetAnimatedPreserveAspectRatio();
      if (preserveAspectRatio) {
        preserveAspectRatio->Init();
        return;
      }
    }

    // Check if this is a transform list attribute going away
    if (GetTransformListAttrName() == aName) {
      SVGAnimatedTransformList* transformList =
          GetExistingAnimatedTransformList();
      if (transformList) {
        transformList->ClearBaseValue();
        return;
      }
    }

    // Check for conditional processing attributes
    nsCOMPtr<SVGTests> tests = do_QueryObject(this);
    if (tests && tests->IsConditionalProcessingAttribute(aName)) {
      tests->UnsetAttr(aName);
      return;
    }

    // Check if this is a string list attribute going away
    StringListAttributesInfo stringListInfo = GetStringListInfo();

    for (uint32_t i = 0; i < stringListInfo.mCount; i++) {
      if (aName == stringListInfo.mInfos[i].mName) {
        stringListInfo.Reset(i);
        return;
      }
    }

    if (aName == nsGkAtoms::_class) {
      mClassAttribute.Init();
      return;
    }
  }

  // Check if this is a string attribute going away
  StringAttributesInfo stringInfo = GetStringInfo();

  for (uint32_t i = 0; i < stringInfo.mCount; i++) {
    if (aNamespaceID == stringInfo.mInfos[i].mNamespaceID &&
        aName == stringInfo.mInfos[i].mName) {
      stringInfo.Reset(i);
      return;
    }
  }
}

void SVGElement::BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                               const nsAttrValue* aValue, bool aNotify) {
  if (!aValue) {
    UnsetAttrInternal(aNamespaceID, aName, aNotify);
  }
  return SVGElementBase::BeforeSetAttr(aNamespaceID, aName, aValue, aNotify);
}

nsChangeHint SVGElement::GetAttributeChangeHint(const nsAtom* aAttribute,
                                                AttrModType aModType) const {
  nsChangeHint retval =
      SVGElementBase::GetAttributeChangeHint(aAttribute, aModType);

  nsCOMPtr<SVGTests> tests = do_QueryObject(const_cast<SVGElement*>(this));
  if (tests && tests->IsConditionalProcessingAttribute(aAttribute)) {
    // It would be nice to only reconstruct the frame if the value returned by
    // SVGTests::PassesConditionalProcessingTests has changed, but we don't
    // know that
    retval |= nsChangeHint_ReconstructFrame;
  }
  return retval;
}

void SVGElement::NodeInfoChanged(Document* aOldDoc) {
  SVGElementBase::NodeInfoChanged(aOldDoc);
}

bool SVGElement::IsNoNamespaceAttrMapped(const nsAtom* name) const {
  if (name == nsGkAtoms::lang) {
    return true;
  }

  if (IsSVGAnimationElement()) {
    return SVGElementBase::IsNoNamespaceAttrMapped(name);
  }

  static const MappedAttributeEntry attributes[] = {
      // Properties that we don't support are commented out.
      // { nsGkAtoms::alignment_baseline },
      {nsGkAtoms::baseline_shift},
      {nsGkAtoms::clip},
      {nsGkAtoms::clip_path},
      {nsGkAtoms::clip_rule},
      {nsGkAtoms::color},
      {nsGkAtoms::color_interpolation},
      {nsGkAtoms::color_interpolation_filters},
      {nsGkAtoms::cursor},
      {nsGkAtoms::direction},
      {nsGkAtoms::display},
      {nsGkAtoms::dominant_baseline},
      {nsGkAtoms::fill},
      {nsGkAtoms::fill_opacity},
      {nsGkAtoms::fill_rule},
      {nsGkAtoms::filter},
      {nsGkAtoms::flood_color},
      {nsGkAtoms::flood_opacity},
      {nsGkAtoms::font_family},
      {nsGkAtoms::font_size},
      {nsGkAtoms::font_size_adjust},
      {nsGkAtoms::font_stretch},
      {nsGkAtoms::font_style},
      {nsGkAtoms::font_variant},
      {nsGkAtoms::font_weight},
      {nsGkAtoms::image_rendering},
      {nsGkAtoms::letter_spacing},
      {nsGkAtoms::lighting_color},
      {nsGkAtoms::marker_end},
      {nsGkAtoms::marker_mid},
      {nsGkAtoms::marker_start},
      {nsGkAtoms::mask},
      {nsGkAtoms::mask_type},
      {nsGkAtoms::opacity},
      {nsGkAtoms::overflow},
      {nsGkAtoms::paint_order},
      {nsGkAtoms::pointer_events},
      {nsGkAtoms::shape_rendering},
      {nsGkAtoms::stop_color},
      {nsGkAtoms::stop_opacity},
      {nsGkAtoms::stroke},
      {nsGkAtoms::stroke_dasharray},
      {nsGkAtoms::stroke_dashoffset},
      {nsGkAtoms::stroke_linecap},
      {nsGkAtoms::stroke_linejoin},
      {nsGkAtoms::stroke_miterlimit},
      {nsGkAtoms::stroke_opacity},
      {nsGkAtoms::stroke_width},
      {nsGkAtoms::text_anchor},
      {nsGkAtoms::text_decoration},
      {nsGkAtoms::text_rendering},
      {nsGkAtoms::transform_origin},
      {nsGkAtoms::unicode_bidi},
      {nsGkAtoms::vector_effect},
      {nsGkAtoms::visibility},
      {nsGkAtoms::white_space},
      {nsGkAtoms::word_spacing},
      {nsGkAtoms::writing_mode},
      {nullptr}};

  static const MappedAttributeEntry* const map[] = {attributes};

  return FindAttributeDependence(name, map) ||
         SVGElementBase::IsNoNamespaceAttrMapped(name);
}

//----------------------------------------------------------------------
// Element methods

// forwarded to Element implementations

//----------------------------------------------------------------------

SVGSVGElement* SVGElement::GetOwnerSVGElement() {
  nsIContent* ancestor = GetFlattenedTreeParent();

  while (ancestor && ancestor->IsSVGElement()) {
    if (ancestor->IsSVGElement(nsGkAtoms::foreignObject)) {
      return nullptr;
    }
    if (auto* svg = SVGSVGElement::FromNode(ancestor)) {
      return svg;
    }
    ancestor = ancestor->GetFlattenedTreeParent();
  }

  // we don't have an ancestor <svg> element...
  return nullptr;
}

SVGElement* SVGElement::GetViewportElement() {
  return SVGContentUtils::GetNearestViewportElement(this);
}

already_AddRefed<DOMSVGAnimatedString> SVGElement::ClassName() {
  return mClassAttribute.ToDOMAnimatedString(this);
}

/* static */
bool SVGElement::UpdateDeclarationBlockFromLength(
    const StyleLockedDeclarationBlock& aBlock, NonCustomCSSPropertyId aPropId,
    const SVGAnimatedLength& aLength, ValToUse aValToUse) {
  float value;
  uint16_t units;
  if (aValToUse == ValToUse::Anim) {
    value = aLength.GetAnimValInSpecifiedUnits();
    units = aLength.GetAnimUnitType();
  } else {
    MOZ_ASSERT(aValToUse == ValToUse::Base);
    value = aLength.GetBaseValInSpecifiedUnits();
    units = aLength.GetBaseUnitType();
  }

  // SVG parser doesn't check non-negativity of some parsed value, we should not
  // pass those to CSS side.
  if (value < 0 &&
      SVGGeometryProperty::IsNonNegativeGeometryProperty(aPropId)) {
    return false;
  }

  nsCSSUnit cssUnit = SVGLength::SpecifiedUnitTypeToCSSUnit(units);

  if (cssUnit == eCSSUnit_Percent) {
    Servo_DeclarationBlock_SetPercentValue(&aBlock, aPropId, value / 100.f);
  } else {
    Servo_DeclarationBlock_SetLengthValue(&aBlock, aPropId, value, cssUnit);
  }

  return true;
}

/* static */
bool SVGElement::UpdateDeclarationBlockFromPath(
    const StyleLockedDeclarationBlock& aBlock,
    const SVGAnimatedPathSegList& aPath, ValToUse aValToUse) {
  const SVGPathData& pathData =
      aValToUse == ValToUse::Anim ? aPath.GetAnimValue() : aPath.GetBaseValue();

  // Based on the current discussion of https://github.com/w3c/svgwg/issues/321,
  // we may have to convert the relative commands into absolute commands.
  // The normalization should be fixed in Bug 1489392.
  Servo_DeclarationBlock_SetPathValue(&aBlock, eCSSProperty_d,
                                      &pathData.RawData());
  return true;
}

template <typename Float>
static StyleTransformOperation MatrixToTransformOperation(
    const gfx::BaseMatrix<Float>& aMatrix) {
  return StyleTransformOperation::Matrix(StyleGenericMatrix<float>{
      .a = float(aMatrix._11),
      .b = float(aMatrix._12),
      .c = float(aMatrix._21),
      .d = float(aMatrix._22),
      .e = float(aMatrix._31),
      .f = float(aMatrix._32),
  });
}

static void SVGTransformToCSS(const SVGTransform& aTransform,
                              nsTArray<StyleTransformOperation>& aOut) {
  switch (aTransform.Type()) {
    case dom::SVGTransform_Binding::SVG_TRANSFORM_SCALE: {
      const auto& m = aTransform.GetMatrix();
      aOut.AppendElement(StyleTransformOperation::Scale(m._11, m._22));
      return;
    }
    case dom::SVGTransform_Binding::SVG_TRANSFORM_TRANSLATE: {
      auto p = aTransform.GetMatrix().GetTranslation();
      aOut.AppendElement(StyleTransformOperation::Translate(
          LengthPercentage::FromPixels(CSSCoord(p.x)),
          LengthPercentage::FromPixels(CSSCoord(p.y))));
      return;
    }
    case dom::SVGTransform_Binding::SVG_TRANSFORM_ROTATE: {
      float cx, cy;
      aTransform.GetRotationOrigin(cx, cy);
      const StyleAngle angle{aTransform.Angle()};
      const bool hasOrigin = cx != 0.0f || cy != 0.0f;
      if (hasOrigin) {
        aOut.AppendElement(StyleTransformOperation::Translate(
            LengthPercentage::FromPixels(cx),
            LengthPercentage::FromPixels(cy)));
      }
      aOut.AppendElement(StyleTransformOperation::Rotate(angle));
      if (hasOrigin) {
        aOut.AppendElement(StyleTransformOperation::Translate(
            LengthPercentage::FromPixels(-cx),
            LengthPercentage::FromPixels(-cy)));
      }
      return;
    }
    case dom::SVGTransform_Binding::SVG_TRANSFORM_SKEWX:
      aOut.AppendElement(StyleTransformOperation::SkewX({aTransform.Angle()}));
      return;
    case dom::SVGTransform_Binding::SVG_TRANSFORM_SKEWY:
      aOut.AppendElement(StyleTransformOperation::SkewY({aTransform.Angle()}));
      return;
    case dom::SVGTransform_Binding::SVG_TRANSFORM_MATRIX: {
      aOut.AppendElement(MatrixToTransformOperation(aTransform.GetMatrix()));
      return;
    }
    case dom::SVGTransform_Binding::SVG_TRANSFORM_UNKNOWN:
    default:
      MOZ_CRASH("Bad SVGTransform?");
  }
}

/* static */
bool SVGElement::UpdateDeclarationBlockFromTransform(
    const StyleLockedDeclarationBlock& aBlock,
    const SVGAnimatedTransformList* aTransform,
    const gfx::Matrix* aAnimateMotionTransform, ValToUse aValToUse) {
  MOZ_ASSERT(aTransform || aAnimateMotionTransform);
  AutoTArray<StyleTransformOperation, 5> operations;
  if (aAnimateMotionTransform) {
    operations.AppendElement(
        MatrixToTransformOperation(*aAnimateMotionTransform));
  }
  if (aTransform) {
    const SVGTransformList& transforms = aValToUse == ValToUse::Anim
                                             ? aTransform->GetAnimValue()
                                             : aTransform->GetBaseValue();
    // TODO: Maybe make SVGTransform use StyleTransformOperation directly?
    for (const auto& transform : transforms) {
      SVGTransformToCSS(transform, operations);
    }
  }
  Servo_DeclarationBlock_SetTransform(&aBlock, eCSSProperty_transform,
                                      &operations);
  return true;
}

//------------------------------------------------------------------------
// Helper class: MappedAttrParser, for parsing values of mapped attributes

namespace {

class MOZ_STACK_CLASS MappedAttrParser {
 public:
  explicit MappedAttrParser(SVGElement& aElement,
                            StyleLockedDeclarationBlock* aDecl)
      : mElement(aElement), mDecl(aDecl) {
    if (mDecl) {
      Servo_DeclarationBlock_Clear(mDecl);
    }
  }
  ~MappedAttrParser() {
    MOZ_ASSERT(!mDecl,
               "If mDecl was initialized, it should have been returned via "
               "TakeDeclarationBlock (and have its pointer cleared)");
  };

  // Parses a mapped attribute value.
  void ParseMappedAttrValue(nsAtom* aMappedAttrName,
                            const nsAString& aMappedAttrValue);

  void MapLang(nsAtom* aLangValue);

  void TellStyleAlreadyParsedResult(nsAtom const* aAtom,
                                    SVGAnimatedLength const& aLength);
  void TellStyleAlreadyParsedResult(const SVGAnimatedPathSegList&);
  void TellStyleAlreadyParsedResult(const SVGAnimatedTransformList&);

  // If we've parsed any values for mapped attributes, this method returns the
  // already_AddRefed declaration block that incorporates the parsed values.
  // Otherwise, this method returns null.
  already_AddRefed<StyleLockedDeclarationBlock> TakeDeclarationBlock() {
    return mDecl.forget();
  }

  StyleLockedDeclarationBlock& EnsureDeclarationBlock() {
    if (!mDecl) {
      mDecl = Servo_DeclarationBlock_CreateEmpty().Consume();
    }
    return *mDecl;
  }

  URLExtraData& EnsureExtraData() {
    if (!mExtraData) {
      mExtraData = mElement.GetURLDataForStyleAttr();
    }
    return *mExtraData;
  }

 private:
  // For reporting use counters
  SVGElement& mElement;

  // Declaration for storing parsed values (lazily initialized).
  RefPtr<StyleLockedDeclarationBlock> mDecl;

  // URL data for parsing stuff. Also lazy.
  RefPtr<URLExtraData> mExtraData;
};

void MappedAttrParser::ParseMappedAttrValue(nsAtom* aMappedAttrName,
                                            const nsAString& aMappedAttrValue) {
  // Get the NonCustomCSSPropertyId id for our mapped attribute.
  NonCustomCSSPropertyId propertyId =
      nsCSSProps::LookupProperty(nsAutoAtomCString(aMappedAttrName));
  MOZ_ASSERT(propertyId != eCSSProperty_UNKNOWN);
  bool changed = false;  // outparam for ParseProperty.
  NS_ConvertUTF16toUTF8 value(aMappedAttrValue);

  auto* doc = mElement.OwnerDoc();
  changed = Servo_DeclarationBlock_SetPropertyById(
      &EnsureDeclarationBlock(), propertyId, &value, false, &EnsureExtraData(),
      StyleParsingMode::ALLOW_UNITLESS_LENGTH, doc->GetCompatibilityMode(),
      &doc->EnsureCSSLoader(), StyleCssRuleType::Style, {});

  // TODO(emilio): If we want to record these from CSSOM more generally, we
  // can pass the document use counters down the FFI call. For now manually
  // count them.
  if (changed && StaticPrefs::layout_css_use_counters_enabled()) {
    UseCounter useCounter = nsCSSProps::UseCounterFor(propertyId);
    MOZ_ASSERT(useCounter != eUseCounter_UNKNOWN);
    doc->SetUseCounter(useCounter);
  }
}

void MappedAttrParser::MapLang(nsAtom* aLangValue) {
  Servo_DeclarationBlock_SetIdentStringValue(&EnsureDeclarationBlock(),
                                             eCSSProperty__x_lang, aLangValue);
}

void MappedAttrParser::TellStyleAlreadyParsedResult(
    nsAtom const* aAtom, SVGAnimatedLength const& aLength) {
  NonCustomCSSPropertyId propertyID =
      nsCSSProps::LookupProperty(nsAutoAtomCString(aAtom));
  SVGElement::UpdateDeclarationBlockFromLength(EnsureDeclarationBlock(),
                                               propertyID, aLength,
                                               SVGElement::ValToUse::Base);
}

void MappedAttrParser::TellStyleAlreadyParsedResult(
    const SVGAnimatedPathSegList& aPath) {
  SVGElement::UpdateDeclarationBlockFromPath(EnsureDeclarationBlock(), aPath,
                                             SVGElement::ValToUse::Base);
}

void MappedAttrParser::TellStyleAlreadyParsedResult(
    const SVGAnimatedTransformList& aTransform) {
  SVGElement::UpdateDeclarationBlockFromTransform(EnsureDeclarationBlock(),
                                                  &aTransform, nullptr,
                                                  SVGElement::ValToUse::Base);
}

}  // namespace

//----------------------------------------------------------------------
// Implementation Helpers:

void SVGElement::UpdateMappedDeclarationBlock() {
  MOZ_ASSERT(IsPendingMappedAttributeEvaluation());
  MappedAttrParser mappedAttrParser(*this, mAttrs.GetMappedDeclarationBlock());

  const bool lengthAffectsStyle =
      SVGGeometryProperty::ElementMapsLengthsToStyle(this);
  uint32_t i = 0;
  const nsAttrValue* xmlLang = nullptr;
  while (BorrowedAttrInfo info = GetAttrInfoAt(i++)) {
    const nsAttrName* attrName = info.mName;
    if (!attrName->IsAtom()) {
      if (!IsAttrMapped(attrName->NamespaceID(), attrName->LocalName())) {
        continue;
      }
      // We map `xml:lang` last, to guarantee it overrides `lang`.
      MOZ_ASSERT(attrName->Equals(nsGkAtoms::lang, kNameSpaceID_XML));
      xmlLang = info.mValue;
      continue;
    }

    nsAtom* nameAtom = attrName->Atom();
    if (!IsNoNamespaceAttrMapped(nameAtom)) {
      continue;
    }

    if (lengthAffectsStyle) {
      auto const* length = GetAnimatedLength(nameAtom);

      if (length && length->HasBaseVal()) {
        // This is an element with geometry property set via SVG attribute,
        // and the attribute is already successfully parsed. We want to go
        // through the optimized path to tell the style system the result
        // directly, rather than let it parse the same thing again.
        mappedAttrParser.TellStyleAlreadyParsedResult(nameAtom, *length);
        continue;
      }
    }

    if (nameAtom == nsGkAtoms::transform ||
        nameAtom == nsGkAtoms::patternTransform ||
        nameAtom == nsGkAtoms::gradientTransform) {
      const auto* transform = GetExistingAnimatedTransformList();
      MOZ_ASSERT(GetTransformListAttrName() == nameAtom);
      MOZ_ASSERT(transform);
      // We want to go through the optimized path to tell the style system the
      // result directly, rather than let it parse the same thing again.
      mappedAttrParser.TellStyleAlreadyParsedResult(*transform);
      continue;
    }

    if (nameAtom == nsGkAtoms::d) {
      const auto* path = GetAnimatedPathSegList();
      // Note: Only SVGPathElement has d attribute.
      MOZ_ASSERT(
          path,
          "SVGPathElement should have the non-null SVGAnimatedPathSegList");
      // The attribute should have been already successfully parsed.
      // We want to go through the optimized path to tell the style system
      // the result directly, rather than let it parse the same thing again.
      mappedAttrParser.TellStyleAlreadyParsedResult(*path);
      // Some other notes:
      // The syntax of CSS d property is different from SVG d attribute.
      // 1. CSS d proeprty accepts:  none | path(<quoted string>);
      // 2. SVG d attribtue accepts: none | <string>
      // So we cannot use css parser to parse the SVG d attribute directly.
      // Besides, |mAttrs.AttrAt(i)| removes the quotes already, so the svg path
      // in |mAttrs.AttrAt(i)| would be something like `M0,0L1,1z` without the
      // quotes. So css tokenizer cannot recognize this as a quoted string, and
      // so svg_path::SVGPathData::parse() doesn't work for this. Fortunately,
      // we still can rely on the parsed result from
      // SVGElement::ParseAttribute() for d attribute.
      continue;
    }

    if (nameAtom == nsGkAtoms::lang) {
      mappedAttrParser.MapLang(info.mValue->GetAtomValue());
      continue;
    }

    nsAutoString value;
    info.mValue->ToString(value);
    mappedAttrParser.ParseMappedAttrValue(nameAtom, value);
  }

  if (xmlLang) {
    mappedAttrParser.MapLang(xmlLang->GetAtomValue());
  }

  // We need to map the SVG view's transform, if there is one.
  if (auto* svg = SVGSVGElement::FromNode(this)) {
    if (const auto* transform = svg->GetViewTransformList()) {
      mappedAttrParser.TellStyleAlreadyParsedResult(*transform);
    }
  }

  mAttrs.SetMappedDeclarationBlock(mappedAttrParser.TakeDeclarationBlock());
}

/**
 * Helper methods for the type-specific WillChangeXXX methods.
 *
 * This method sends out appropriate pre-change notifications so that selector
 * restyles (e.g. due to changes that cause |elem[attr="val"]| to start/stop
 * matching) work.
 *
 * The nsAttrValue returned by this method depends on whether there are
 * mutation event listeners listening for changes to this element's attributes.
 * If not, then the object returned is empty. If there are, then the
 * nsAttrValue returned contains a serialized copy of the attribute's value
 * prior to the change, and this object should be passed to the corresponding
 * DidChangeXXX method call (assuming a WillChangeXXX call is required for the
 * SVG type - see comment below). This is necessary so that the 'prevValue'
 * property of the mutation event that is dispatched will correctly contain the
 * old value.
 *
 * The reason we don't handle the old value is, it's expensive to serialize the
 * old value. This is especially true for list type attributes, which may be
 * built up via the SVG DOM resulting in a large number of Will/DidModifyXXX
 * calls before the script finally finishes setting the attribute.
 *
 * Note that unlike using SetParsedAttr, using Will/DidChangeXXX does NOT check
 * and filter out redundant changes. Before calling WillChangeXXX, the caller
 * should check whether the new and old values are actually the same, and skip
 * calling Will/DidChangeXXX if they are.
 *
 * Also note that not all SVG types use this scheme. For types that can be
 * represented by an nsAttrValue without pointing back to an SVG object (e.g.
 * enums, booleans, integers) we can simply use SetParsedAttr which will do all
 * of the above for us. For such types there is no matching WillChangeXXX
 * method, only DidChangeXXX which calls SetParsedAttr.
 */
void SVGElement::WillChangeValue(nsAtom* aName,
                                 const mozAutoDocUpdate& aProofOfUpdate) {
  const nsAttrValue* attrValue = GetParsedAttr(aName);
  const AttrModType modType =
      attrValue ? AttrModType::Modification : AttrModType::Addition;
  MutationObservers::NotifyAttributeWillChange(this, kNameSpaceID_None, aName,
                                               modType);

  // This is not strictly correct--the attribute value parameter for
  // BeforeSetAttr should reflect the value that *will* be set but that implies
  // allocating, e.g. an extra SVGAnimatedLength, and isn't necessary at the
  // moment since no SVG elements overload BeforeSetAttr. For now we just pass
  // the current value.
  const nsAttrValue emptyAttrValue;
  const nsAttrValue* value = attrValue ? attrValue : &emptyAttrValue;
  BeforeSetAttr(kNameSpaceID_None, aName, value, kNotifyDocumentObservers);
}

/**
 * Helper methods for the type-specific DidChangeXXX methods.
 *
 * aNewValue is replaced with the old value.
 */
void SVGElement::DidChangeValue(nsAtom* aName, nsAttrValue& aNewValue,
                                const mozAutoDocUpdate& aProofOfUpdate) {
  // XXX Really, the fourth argument to SetAttrAndNotify should be null if
  // emptyValue does not represent the actual previous value of the attribute,
  // but currently SVG elements do not even use the old attribute value in
  // |AfterSetAttr|, so this should be ok.
  const AttrModType modType =
      HasAttr(aName) ? AttrModType::Modification : AttrModType::Addition;
  const nsAttrValue emptyValue;
  SetAttrAndNotify(kNameSpaceID_None, aName, nullptr, &emptyValue, aNewValue,
                   nullptr, modType, kNotifyDocumentObservers,
                   kCallAfterSetAttr, aProofOfUpdate, IsKnownNewAttr::No);
}

nsAtom* SVGElement::GetEventNameForAttr(nsAtom* aAttr) {
  if (IsSVGElement(nsGkAtoms::svg)) {
    if (aAttr == nsGkAtoms::onload) return nsGkAtoms::onSVGLoad;
    if (aAttr == nsGkAtoms::onscroll) return nsGkAtoms::onSVGScroll;
  }
  if (aAttr == nsGkAtoms::onbegin) return nsGkAtoms::onbeginEvent;
  if (aAttr == nsGkAtoms::onrepeat) return nsGkAtoms::onrepeatEvent;
  if (aAttr == nsGkAtoms::onend) return nsGkAtoms::onendEvent;

  return SVGElementBase::GetEventNameForAttr(aAttr);
}

SVGViewportElement* SVGElement::GetCtx() const {
  return SVGContentUtils::GetNearestViewportElement(this);
}

/* virtual */
gfxMatrix SVGElement::ChildToUserSpaceTransform() const { return {}; }

SVGElement::LengthAttributesInfo SVGElement::GetLengthInfo() {
  return LengthAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::SetLength(nsAtom* aName, const SVGAnimatedLength& aLength) {
  LengthAttributesInfo lengthInfo = GetLengthInfo();

  for (uint32_t i = 0; i < lengthInfo.mCount; i++) {
    if (aName == lengthInfo.mInfos[i].mName) {
      lengthInfo.mValues[i] = aLength;
      DidAnimateLength(i);
      return;
    }
  }
  MOZ_ASSERT(false, "no length found to set");
}

void SVGElement::WillChangeLength(uint8_t aAttrEnum,
                                  const mozAutoDocUpdate& aProofOfUpdate) {
  WillChangeValue(GetLengthInfo().mInfos[aAttrEnum].mName, aProofOfUpdate);
}

void SVGElement::DidChangeLength(uint8_t aAttrEnum,
                                 const mozAutoDocUpdate& aProofOfUpdate) {
  LengthAttributesInfo info = GetLengthInfo();

  NS_ASSERTION(info.mCount > 0,
               "DidChangeLength on element with no length attribs");
  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue newValue;
  newValue.SetTo(info.mValues[aAttrEnum], nullptr);

  DidChangeValue(info.mInfos[aAttrEnum].mName, newValue, aProofOfUpdate);
}

void SVGElement::DidAnimateLength(uint8_t aAttrEnum) {
  // We need to do this here. Normally the SMIL restyle would also cause us to
  // do this from DidSetComputedStyle, but we don't have that guarantee if our
  // frame gets reconstructed.
  ClearAnyCachedPath();

  if (SVGGeometryProperty::ElementMapsLengthsToStyle(this)) {
    NonCustomCSSPropertyId propId =
        SVGGeometryProperty::AttrEnumToCSSPropId(this, aAttrEnum);

    // We don't map use element width/height currently. We can remove this
    // test when we do.
    if (propId != eCSSProperty_UNKNOWN) {
      auto lengthInfo = GetLengthInfo();
      if (lengthInfo.mValues[aAttrEnum].IsAnimated()) {
        SMILOverrideStyle()->SetSMILValue(propId,
                                          lengthInfo.mValues[aAttrEnum]);
      } else {
        SMILOverrideStyle()->ClearSMILValue(propId);
      }
    }
  }

  auto info = GetLengthInfo();
  DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
}

SVGAnimatedLength* SVGElement::GetAnimatedLength(uint8_t aAttrEnum) {
  LengthAttributesInfo info = GetLengthInfo();
  if (aAttrEnum < info.mCount) {
    return &info.mValues[aAttrEnum];
  }
  MOZ_ASSERT_UNREACHABLE("Bad attrEnum");
  return nullptr;
}

SVGAnimatedLength* SVGElement::GetAnimatedLength(const nsAtom* aAttrName) {
  LengthAttributesInfo lengthInfo = GetLengthInfo();

  for (uint32_t i = 0; i < lengthInfo.mCount; i++) {
    if (aAttrName == lengthInfo.mInfos[i].mName) {
      return &lengthInfo.mValues[i];
    }
  }
  return nullptr;
}

void SVGElement::GetAnimatedLengthValues(float* aFirst, ...) {
  LengthAttributesInfo info = GetLengthInfo();

  NS_ASSERTION(info.mCount > 0,
               "GetAnimatedLengthValues on element with no length attribs");

  SVGElementMetrics metrics(this);

  float* f = aFirst;
  uint32_t i = 0;

  va_list args;
  va_start(args, aFirst);

  while (f && i < info.mCount) {
    *f = info.mValues[i++].GetAnimValueWithZoom(metrics);
    f = va_arg(args, float*);
  }

  va_end(args);
}

SVGElement::LengthListAttributesInfo SVGElement::GetLengthListInfo() {
  return LengthListAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::WillChangeLengthList(uint8_t aAttrEnum,
                                      const mozAutoDocUpdate& aProofOfUpdate) {
  WillChangeValue(GetLengthListInfo().mInfos[aAttrEnum].mName, aProofOfUpdate);
}

void SVGElement::DidChangeLengthList(uint8_t aAttrEnum,
                                     const mozAutoDocUpdate& aProofOfUpdate) {
  LengthListAttributesInfo info = GetLengthListInfo();

  NS_ASSERTION(info.mCount > 0,
               "DidChangeLengthList on element with no length list attribs");
  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue newValue;
  newValue.SetTo(info.mValues[aAttrEnum].GetBaseValue(), nullptr);

  DidChangeValue(info.mInfos[aAttrEnum].mName, newValue, aProofOfUpdate);
}

void SVGElement::GetAnimatedLengthListValues(SVGUserUnitList* aFirst, ...) {
  LengthListAttributesInfo info = GetLengthListInfo();

  NS_ASSERTION(
      info.mCount > 0,
      "GetAnimatedLengthListValues on element with no length list attribs");

  SVGUserUnitList* list = aFirst;
  uint32_t i = 0;

  va_list args;
  va_start(args, aFirst);

  while (list && i < info.mCount) {
    list->Init(&(info.mValues[i].GetAnimValue()), this, info.mInfos[i].mAxis);
    ++i;
    list = va_arg(args, SVGUserUnitList*);
  }

  va_end(args);
}

SVGAnimatedLengthList* SVGElement::GetAnimatedLengthList(uint8_t aAttrEnum) {
  LengthListAttributesInfo info = GetLengthListInfo();
  if (aAttrEnum < info.mCount) {
    return &(info.mValues[aAttrEnum]);
  }
  MOZ_ASSERT_UNREACHABLE("Bad attrEnum");
  return nullptr;
}

SVGElement::NumberListAttributesInfo SVGElement::GetNumberListInfo() {
  return NumberListAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::WillChangeNumberList(uint8_t aAttrEnum,
                                      const mozAutoDocUpdate& aProofOfUpdate) {
  WillChangeValue(GetNumberListInfo().mInfos[aAttrEnum].mName, aProofOfUpdate);
}

void SVGElement::DidChangeNumberList(uint8_t aAttrEnum,
                                     const mozAutoDocUpdate& aProofOfUpdate) {
  NumberListAttributesInfo info = GetNumberListInfo();

  MOZ_ASSERT(info.mCount > 0,
             "DidChangeNumberList on element with no number list attribs");
  MOZ_ASSERT(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue newValue;
  newValue.SetTo(info.mValues[aAttrEnum].GetBaseValue(), nullptr);

  DidChangeValue(info.mInfos[aAttrEnum].mName, newValue, aProofOfUpdate);
}

SVGAnimatedNumberList* SVGElement::GetAnimatedNumberList(uint8_t aAttrEnum) {
  NumberListAttributesInfo info = GetNumberListInfo();
  if (aAttrEnum < info.mCount) {
    return &(info.mValues[aAttrEnum]);
  }
  MOZ_ASSERT(false, "Bad attrEnum");
  return nullptr;
}

SVGAnimatedNumberList* SVGElement::GetAnimatedNumberList(nsAtom* aAttrName) {
  NumberListAttributesInfo info = GetNumberListInfo();
  for (uint32_t i = 0; i < info.mCount; i++) {
    if (aAttrName == info.mInfos[i].mName) {
      return &info.mValues[i];
    }
  }
  MOZ_ASSERT(false, "Bad caller");
  return nullptr;
}

void SVGElement::WillChangePointList(const mozAutoDocUpdate& aProofOfUpdate) {
  MOZ_ASSERT(GetPointListAttrName(), "Changing non-existent point list?");
  WillChangeValue(GetPointListAttrName(), aProofOfUpdate);
}

void SVGElement::DidChangePointList(const mozAutoDocUpdate& aProofOfUpdate) {
  MOZ_ASSERT(GetPointListAttrName(), "Changing non-existent point list?");

  nsAttrValue newValue;
  newValue.SetTo(GetAnimatedPointList()->GetBaseValue(), nullptr);

  DidChangeValue(GetPointListAttrName(), newValue, aProofOfUpdate);
}

void SVGElement::DidAnimatePointList() {
  MOZ_ASSERT(GetPointListAttrName(), "Animating non-existent path data?");

  ClearAnyCachedPath();

  DidAnimateAttribute(kNameSpaceID_None, GetPointListAttrName());
}

void SVGElement::WillChangePathSegList(const mozAutoDocUpdate& aProofOfUpdate) {
  MOZ_ASSERT(GetPathDataAttrName(), "Changing non-existent path seg list?");
  WillChangeValue(GetPathDataAttrName(), aProofOfUpdate);
}

void SVGElement::DidChangePathSegList(const mozAutoDocUpdate& aProofOfUpdate) {
  MOZ_ASSERT(GetPathDataAttrName(), "Changing non-existent path seg list?");

  nsAttrValue newValue;
  newValue.SetTo(GetAnimatedPathSegList()->GetBaseValue(), nullptr);

  DidChangeValue(GetPathDataAttrName(), newValue, aProofOfUpdate);
}

void SVGElement::DidAnimatePathSegList() {
  nsStaticAtom* name = GetPathDataAttrName();
  MOZ_ASSERT(name, "Animating non-existent path data?");

  ClearAnyCachedPath();

  // Notify style we have to update the d property because of SMIL animation.
  if (name == nsGkAtoms::d) {
    auto* animPathSegList = GetAnimatedPathSegList();
    if (animPathSegList->IsAnimating()) {
      SMILOverrideStyle()->SetSMILValue(eCSSProperty_d, *animPathSegList);
    } else {
      SMILOverrideStyle()->ClearSMILValue(eCSSProperty_d);
    }
  }

  DidAnimateAttribute(kNameSpaceID_None, name);
}

SVGElement::NumberAttributesInfo SVGElement::GetNumberInfo() {
  return NumberAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::DidChangeNumber(uint8_t aAttrEnum) {
  NumberAttributesInfo info = GetNumberInfo();

  NS_ASSERTION(info.mCount > 0,
               "DidChangeNumber on element with no number attribs");
  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue attrValue;
  attrValue.SetTo(info.mValues[aAttrEnum].GetBaseValue(), nullptr);

  SetParsedAttr(kNameSpaceID_None, info.mInfos[aAttrEnum].mName, nullptr,
                attrValue, true, mozilla::dom::IsKnownNewAttr::No);
}

void SVGElement::GetAnimatedNumberValues(float* aFirst, ...) {
  NumberAttributesInfo info = GetNumberInfo();

  NS_ASSERTION(info.mCount > 0,
               "GetAnimatedNumberValues on element with no number attribs");

  float* f = aFirst;
  uint32_t i = 0;

  va_list args;
  va_start(args, aFirst);

  while (f && i < info.mCount) {
    *f = info.mValues[i++].GetAnimValue();
    f = va_arg(args, float*);
  }
  va_end(args);
}

SVGElement::NumberPairAttributesInfo SVGElement::GetNumberPairInfo() {
  return NumberPairAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::WillChangeNumberPair(uint8_t aAttrEnum) {
  mozAutoDocUpdate updateBatch(GetComposedDoc(), kDontNotifyDocumentObservers);
  WillChangeValue(GetNumberPairInfo().mInfos[aAttrEnum].mName, updateBatch);
}

void SVGElement::DidChangeNumberPair(uint8_t aAttrEnum) {
  NumberPairAttributesInfo info = GetNumberPairInfo();

  NS_ASSERTION(info.mCount > 0,
               "DidChangePairNumber on element with no number pair attribs");
  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue newValue;
  newValue.SetTo(info.mValues[aAttrEnum], nullptr);

  mozAutoDocUpdate updateBatch(GetComposedDoc(), kNotifyDocumentObservers);
  DidChangeValue(info.mInfos[aAttrEnum].mName, newValue, updateBatch);
}

SVGElement::IntegerAttributesInfo SVGElement::GetIntegerInfo() {
  return IntegerAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::DidChangeInteger(uint8_t aAttrEnum) {
  IntegerAttributesInfo info = GetIntegerInfo();
  NS_ASSERTION(info.mCount > 0,
               "DidChangeInteger on element with no integer attribs");
  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue attrValue;
  attrValue.SetTo(info.mValues[aAttrEnum].GetBaseValue(), nullptr);

  SetParsedAttr(kNameSpaceID_None, info.mInfos[aAttrEnum].mName, nullptr,
                attrValue, true, mozilla::dom::IsKnownNewAttr::No);
}

void SVGElement::GetAnimatedIntegerValues(int32_t* aFirst, ...) {
  IntegerAttributesInfo info = GetIntegerInfo();

  NS_ASSERTION(info.mCount > 0,
               "GetAnimatedIntegerValues on element with no integer attribs");

  int32_t* n = aFirst;
  uint32_t i = 0;

  va_list args;
  va_start(args, aFirst);

  while (n && i < info.mCount) {
    *n = info.mValues[i++].GetAnimValue();
    n = va_arg(args, int32_t*);
  }
  va_end(args);
}

SVGElement::IntegerPairAttributesInfo SVGElement::GetIntegerPairInfo() {
  return IntegerPairAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::WillChangeIntegerPair(uint8_t aAttrEnum,
                                       const mozAutoDocUpdate& aProofOfUpdate) {
  WillChangeValue(GetIntegerPairInfo().mInfos[aAttrEnum].mName, aProofOfUpdate);
}

void SVGElement::DidChangeIntegerPair(uint8_t aAttrEnum,
                                      const mozAutoDocUpdate& aProofOfUpdate) {
  IntegerPairAttributesInfo info = GetIntegerPairInfo();

  NS_ASSERTION(info.mCount > 0,
               "DidChangeIntegerPair on element with no integer pair attribs");
  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue newValue;
  newValue.SetTo(info.mValues[aAttrEnum], nullptr);

  DidChangeValue(info.mInfos[aAttrEnum].mName, newValue, aProofOfUpdate);
}

SVGElement::BooleanAttributesInfo SVGElement::GetBooleanInfo() {
  return BooleanAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::DidChangeBoolean(uint8_t aAttrEnum) {
  BooleanAttributesInfo info = GetBooleanInfo();

  NS_ASSERTION(info.mCount > 0,
               "DidChangeBoolean on element with no boolean attribs");
  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue attrValue(info.mValues[aAttrEnum].GetBaseValueAtom());
  SetParsedAttr(kNameSpaceID_None, info.mInfos[aAttrEnum].mName, nullptr,
                attrValue, true, mozilla::dom::IsKnownNewAttr::No);
}

SVGElement::EnumAttributesInfo SVGElement::GetEnumInfo() {
  return EnumAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::DidChangeEnum(uint8_t aAttrEnum) {
  EnumAttributesInfo info = GetEnumInfo();

  NS_ASSERTION(info.mCount > 0,
               "DidChangeEnum on element with no enum attribs");
  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  nsAttrValue attrValue(info.mValues[aAttrEnum].GetBaseValueAtom(this));
  SetParsedAttr(kNameSpaceID_None, info.mInfos[aAttrEnum].mName, nullptr,
                attrValue, true, mozilla::dom::IsKnownNewAttr::No);
}

SVGAnimatedOrient* SVGElement::GetAnimatedOrient() { return nullptr; }

void SVGElement::WillChangeOrient(const mozAutoDocUpdate& aProofOfUpdate) {
  WillChangeValue(nsGkAtoms::orient, aProofOfUpdate);
}

void SVGElement::DidChangeOrient(const mozAutoDocUpdate& aProofOfUpdate) {
  SVGAnimatedOrient* orient = GetAnimatedOrient();

  NS_ASSERTION(orient, "DidChangeOrient on element with no orient attrib");

  nsAttrValue newValue;
  newValue.SetTo(*orient, nullptr);

  DidChangeValue(nsGkAtoms::orient, newValue, aProofOfUpdate);
}

SVGAnimatedViewBox* SVGElement::GetAnimatedViewBox() { return nullptr; }

void SVGElement::WillChangeViewBox(const mozAutoDocUpdate& aProofOfUpdate) {
  WillChangeValue(nsGkAtoms::viewBox, aProofOfUpdate);
}

void SVGElement::DidChangeViewBox(const mozAutoDocUpdate& aProofOfUpdate) {
  SVGAnimatedViewBox* viewBox = GetAnimatedViewBox();

  NS_ASSERTION(viewBox, "DidChangeViewBox on element with no viewBox attrib");

  nsAttrValue newValue;
  newValue.SetTo(*viewBox, nullptr);

  DidChangeValue(nsGkAtoms::viewBox, newValue, aProofOfUpdate);
}

SVGAnimatedPreserveAspectRatio* SVGElement::GetAnimatedPreserveAspectRatio() {
  return nullptr;
}

void SVGElement::WillChangePreserveAspectRatio(
    const mozAutoDocUpdate& aProofOfUpdate) {
  WillChangeValue(nsGkAtoms::preserveAspectRatio, aProofOfUpdate);
}

void SVGElement::DidChangePreserveAspectRatio(
    const mozAutoDocUpdate& aProofOfUpdate) {
  SVGAnimatedPreserveAspectRatio* preserveAspectRatio =
      GetAnimatedPreserveAspectRatio();

  NS_ASSERTION(preserveAspectRatio,
               "DidChangePreserveAspectRatio on element with no "
               "preserveAspectRatio attrib");

  nsAttrValue newValue;
  newValue.SetTo(*preserveAspectRatio, nullptr);

  DidChangeValue(nsGkAtoms::preserveAspectRatio, newValue, aProofOfUpdate);
}

void SVGElement::WillChangeTransformList(
    const mozAutoDocUpdate& aProofOfUpdate) {
  WillChangeValue(GetTransformListAttrName(), aProofOfUpdate);
}

void SVGElement::DidChangeTransformList(
    const mozAutoDocUpdate& aProofOfUpdate) {
  MOZ_ASSERT(GetTransformListAttrName(),
             "Changing non-existent transform list?");

  // The transform attribute is being set, so we must ensure that the
  // SVGAnimatedTransformList is/has been allocated:
  nsAttrValue newValue;
  newValue.SetTo(GetOrCreateAnimatedTransformList()->GetBaseValue(), nullptr);

  DidChangeValue(GetTransformListAttrName(), newValue, aProofOfUpdate);
}

void SVGElement::DidAnimateTransformList() {
  MOZ_ASSERT(GetTransformListAttrName(),
             "Animating non-existent transform data?");
  const auto* animTransformList = GetExistingAnimatedTransformList();
  const auto* animateMotion = GetAnimateMotionTransform();
  if (animateMotion ||
      (animTransformList && animTransformList->IsAnimating())) {
    SMILOverrideStyle()->SetSMILValue(eCSSProperty_transform, animTransformList,
                                      animateMotion);
  } else {
    SMILOverrideStyle()->ClearSMILValue(eCSSProperty_transform);
  }
}

SVGElement::StringAttributesInfo SVGElement::GetStringInfo() {
  return StringAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::GetStringBaseValue(uint8_t aAttrEnum,
                                    nsAString& aResult) const {
  SVGElement::StringAttributesInfo info =
      const_cast<SVGElement*>(this)->GetStringInfo();

  NS_ASSERTION(info.mCount > 0,
               "GetBaseValue on element with no string attribs");

  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  GetAttr(info.mInfos[aAttrEnum].mNamespaceID, info.mInfos[aAttrEnum].mName,
          aResult);
}

void SVGElement::SetStringBaseValue(uint8_t aAttrEnum,
                                    const nsAString& aValue) {
  SVGElement::StringAttributesInfo info = GetStringInfo();

  NS_ASSERTION(info.mCount > 0,
               "SetBaseValue on element with no string attribs");

  NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

  SetAttr(info.mInfos[aAttrEnum].mNamespaceID, info.mInfos[aAttrEnum].mName,
          aValue, true, IsKnownNewAttr::No);
}

SVGElement::StringListAttributesInfo SVGElement::GetStringListInfo() {
  return StringListAttributesInfo(nullptr, nullptr, 0);
}

void SVGElement::WillChangeStringList(bool aIsConditionalProcessingAttribute,
                                      uint8_t aAttrEnum,
                                      const mozAutoDocUpdate& aProofOfUpdate) {
  nsStaticAtom* name;
  if (aIsConditionalProcessingAttribute) {
    nsCOMPtr<SVGTests> tests(do_QueryInterface(this));
    name = tests->GetAttrName(aAttrEnum);
  } else {
    name = GetStringListInfo().mInfos[aAttrEnum].mName;
  }
  WillChangeValue(name, aProofOfUpdate);
}

void SVGElement::DidChangeStringList(bool aIsConditionalProcessingAttribute,
                                     uint8_t aAttrEnum,
                                     const mozAutoDocUpdate& aProofOfUpdate) {
  nsStaticAtom* name;
  nsAttrValue newValue;
  nsCOMPtr<SVGTests> tests;

  if (aIsConditionalProcessingAttribute) {
    tests = do_QueryObject(this);
    name = tests->GetAttrName(aAttrEnum);
    tests->GetAttrValue(aAttrEnum, newValue);
  } else {
    StringListAttributesInfo info = GetStringListInfo();

    NS_ASSERTION(info.mCount > 0,
                 "DidChangeStringList on element with no string list attribs");
    NS_ASSERTION(aAttrEnum < info.mCount, "aAttrEnum out of range");

    name = info.mInfos[aAttrEnum].mName;
    newValue.SetTo(info.mValues[aAttrEnum], nullptr);
  }

  DidChangeValue(name, newValue, aProofOfUpdate);

  if (aIsConditionalProcessingAttribute) {
    tests->MaybeInvalidate();
  }
}

void SVGElement::DidAnimateAttribute(int32_t aNameSpaceID, nsAtom* aAttribute) {
  if (auto* frame = GetPrimaryFrame()) {
    frame->AttributeChanged(aNameSpaceID, aAttribute,
                            AttrModType::Modification);
    SVGObserverUtils::InvalidateRenderingObservers(frame);
    return;
  }
  SVGObserverUtils::InvalidateDirectRenderingObservers(this);
}

nsresult SVGElement::ReportAttributeParseFailure(Document* aDocument,
                                                 nsAtom* aAttribute,
                                                 const nsAString& aValue) {
  AutoTArray<nsString, 2> strings;
  strings.AppendElement(nsDependentAtomString(aAttribute));
  strings.AppendElement(aValue);
  return SVGContentUtils::ReportToConsole(aDocument, "AttributeParseWarning",
                                          strings);
}

std::unique_ptr<SMILAttr> SVGElement::GetAnimatedAttr(int32_t aNamespaceID,
                                                      nsAtom* aName) {
  if (aNamespaceID == kNameSpaceID_None) {
    // Transforms:
    if (GetTransformListAttrName() == aName) {
      // The transform attribute is being animated, so we must ensure that the
      // SVGAnimatedTransformList is/has been allocated:
      return GetOrCreateAnimatedTransformList()->ToSMILAttr(this);
    }

    // Motion (fake 'attribute' for animateMotion)
    if (aName == nsGkAtoms::mozAnimateMotionDummyAttr) {
      return std::make_unique<SVGMotionSMILAttr>(this);
    }

    // Lengths:
    LengthAttributesInfo info = GetLengthInfo();
    for (uint32_t i = 0; i < info.mCount; i++) {
      if (aName == info.mInfos[i].mName) {
        return info.mValues[i].ToSMILAttr(this);
      }
    }

    // Numbers:
    {
      NumberAttributesInfo info = GetNumberInfo();
      for (uint32_t i = 0; i < info.mCount; i++) {
        if (aName == info.mInfos[i].mName) {
          return info.mValues[i].ToSMILAttr(this);
        }
      }
    }

    // Number Pairs:
    {
      NumberPairAttributesInfo info = GetNumberPairInfo();
      for (uint32_t i = 0; i < info.mCount; i++) {
        if (aName == info.mInfos[i].mName) {
          return info.mValues[i].ToSMILAttr(this);
        }
      }
    }

    // Integers:
    {
      IntegerAttributesInfo info = GetIntegerInfo();
      for (uint32_t i = 0; i < info.mCount; i++) {
        if (aName == info.mInfos[i].mName) {
          return info.mValues[i].ToSMILAttr(this);
        }
      }
    }

    // Integer Pairs:
    {
      IntegerPairAttributesInfo info = GetIntegerPairInfo();
      for (uint32_t i = 0; i < info.mCount; i++) {
        if (aName == info.mInfos[i].mName) {
          return info.mValues[i].ToSMILAttr(this);
        }
      }
    }

    // Enumerations:
    {
      EnumAttributesInfo info = GetEnumInfo();
      for (uint32_t i = 0; i < info.mCount; i++) {
        if (aName == info.mInfos[i].mName) {
          return info.mValues[i].ToSMILAttr(this);
        }
      }
    }

    // Booleans:
    {
      BooleanAttributesInfo info = GetBooleanInfo();
      for (uint32_t i = 0; i < info.mCount; i++) {
        if (aName == info.mInfos[i].mName) {
          return info.mValues[i].ToSMILAttr(this);
        }
      }
    }

    // orient:
    if (aName == nsGkAtoms::orient) {
      SVGAnimatedOrient* orient = GetAnimatedOrient();
      return orient ? orient->ToSMILAttr(this) : nullptr;
    }

    // viewBox:
    if (aName == nsGkAtoms::viewBox) {
      SVGAnimatedViewBox* viewBox = GetAnimatedViewBox();
      return viewBox ? viewBox->ToSMILAttr(this) : nullptr;
    }

    // preserveAspectRatio:
    if (aName == nsGkAtoms::preserveAspectRatio) {
      SVGAnimatedPreserveAspectRatio* preserveAspectRatio =
          GetAnimatedPreserveAspectRatio();
      return preserveAspectRatio ? preserveAspectRatio->ToSMILAttr(this)
                                 : nullptr;
    }

    // NumberLists:
    {
      NumberListAttributesInfo info = GetNumberListInfo();
      for (uint32_t i = 0; i < info.mCount; i++) {
        if (aName == info.mInfos[i].mName) {
          MOZ_ASSERT(i <= UCHAR_MAX, "Too many attributes");
          return info.mValues[i].ToSMILAttr(this, uint8_t(i));
        }
      }
    }

    // LengthLists:
    {
      LengthListAttributesInfo info = GetLengthListInfo();
      for (uint32_t i = 0; i < info.mCount; i++) {
        if (aName == info.mInfos[i].mName) {
          MOZ_ASSERT(i <= UCHAR_MAX, "Too many attributes");
          return info.mValues[i].ToSMILAttr(this, uint8_t(i),
                                            info.mInfos[i].mAxis,
                                            info.mInfos[i].mCouldZeroPadList);
        }
      }
    }

    // PointLists:
    {
      if (GetPointListAttrName() == aName) {
        SVGAnimatedPointList* pointList = GetAnimatedPointList();
        if (pointList) {
          return pointList->ToSMILAttr(this);
        }
      }
    }

    // PathSegLists:
    {
      if (GetPathDataAttrName() == aName) {
        SVGAnimatedPathSegList* segList = GetAnimatedPathSegList();
        if (segList) {
          return segList->ToSMILAttr(this);
        }
      }
    }

    if (aName == nsGkAtoms::_class) {
      return mClassAttribute.ToSMILAttr(this);
    }
  }

  // Strings
  {
    StringAttributesInfo info = GetStringInfo();
    for (uint32_t i = 0; i < info.mCount; i++) {
      if (aNamespaceID == info.mInfos[i].mNamespaceID &&
          aName == info.mInfos[i].mName) {
        return info.mValues[i].ToSMILAttr(this);
      }
    }
  }

  return nullptr;
}

void SVGElement::AnimationNeedsResample() {
  Document* doc = GetComposedDoc();
  if (doc && doc->HasAnimationController()) {
    doc->GetAnimationController()->SetResampleNeeded();
  }
}

void SVGElement::FlushAnimations() {
  Document* doc = GetComposedDoc();
  if (doc && doc->HasAnimationController()) {
    doc->GetAnimationController()->FlushResampleRequests();
  }
}

void SVGElement::AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                        size_t* aNodeSize) const {
  Element::AddSizeOfExcludingThis(aSizes, aNodeSize);
}

}  // namespace mozilla::dom
