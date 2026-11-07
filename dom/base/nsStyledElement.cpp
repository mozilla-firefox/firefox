/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsStyledElement.h"

#include "mozAutoDocUpdate.h"
#include "mozilla/DeclarationBlock.h"
#include "mozilla/RefPtr.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/StylePropertyMap.h"
#include "nsAttrValue.h"
#include "nsAttrValueInlines.h"
#include "nsContentUtils.h"
#include "nsDOMCSSAttrDeclaration.h"
#include "nsDOMCSSDeclaration.h"
#include "nsGkAtoms.h"
#include "nsIMutationObserver.h"
#include "nsServiceManagerUtils.h"
#include "nsStyleUtil.h"
#include "nsXULElement.h"

using namespace mozilla;
using namespace mozilla::dom;

// Use the CC variant of this, even though this class does not define
// a new CC participant, to make QIing to the CC interfaces faster.
NS_IMPL_QUERY_INTERFACE_CYCLE_COLLECTION_INHERITED(nsStyledElement,
                                                   nsStyledElementBase,
                                                   nsStyledElement)

//----------------------------------------------------------------------
// nsIContent methods

bool nsStyledElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                     const nsAString& aValue,
                                     nsIPrincipal* aMaybeScriptedPrincipal,
                                     nsAttrValue& aResult) {
  if (aAttribute == nsGkAtoms::style && aNamespaceID == kNameSpaceID_None) {
    ParseStyleAttribute(aValue, aMaybeScriptedPrincipal, aResult, false);
    return true;
  }

  return nsStyledElementBase::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                             aMaybeScriptedPrincipal, aResult);
}

void nsStyledElement::BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                    const nsAttrValue* aValue, bool aNotify) {
  if (aNamespaceID == kNameSpaceID_None && aName == nsGkAtoms::style &&
      aValue) {
    SetMayHaveStyle();
  }

  return nsStyledElementBase::BeforeSetAttr(aNamespaceID, aName, aValue,
                                            aNotify);
}

void nsStyledElement::InlineStyleDeclarationWillChange(
    MutationClosureData& aData) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(OwnerDoc()->UpdateNestingLevel() > 0,
             "Should be inside document update!");
  bool modification = false;
  if (MayHaveStyle()) {
    CustomElementDefinition* definition = GetCustomElementDefinition();
    if (definition && definition->IsInObservedAttributeList(nsGkAtoms::style)) {
      nsAutoString oldValueStr;
      modification = GetAttr(nsGkAtoms::style, oldValueStr);
      if (modification) {
        aData.mOldValue.emplace();
        aData.mOldValue->SetTo(oldValueStr);
      }
    } else {
      modification = HasAttr(nsGkAtoms::style);
    }
  }

  aData.mModType =
      modification ? AttrModType::Modification : AttrModType::Addition;
  MutationObservers::NotifyAttributeWillChange(
      this, kNameSpaceID_None, nsGkAtoms::style, aData.mModType);

  // XXXsmaug In order to make attribute handling more consistent, consider to
  //         call BeforeSetAttr and pass kCallAfterSetAttr to
  //         SetAttrAndNotify in SetInlineStyleDeclaration.
  //         Handling of mozAutoDocUpdate may require changes in that case.
}

nsresult nsStyledElement::SetInlineStyleDeclaration(
    StyleLockedDeclarationBlock& aDeclaration, MutationClosureData& aData) {
  MOZ_ASSERT(OwnerDoc()->UpdateNestingLevel(),
             "Should be inside document update!");

  nsAttrValue attrValue(
      MakeAndAddRef<DeclarationBlock>(do_AddRef(&aDeclaration)), nullptr);
  SetMayHaveStyle();

  const bool kNotify = true;
  // TODO(emilio): given the MOZ_ASSERT above, isn't this updateBatch just
  // redundant? Can we get it from the caller or so?
  mozAutoDocUpdate updateBatch(GetComposedDoc(), kNotify);
  return SetAttrAndNotify(kNameSpaceID_None, nsGkAtoms::style, nullptr,
                          aData.mOldValue.ptrOr(nullptr), attrValue, nullptr,
                          aData.mModType, kNotify, kDontCallAfterSetAttr,
                          updateBatch, IsKnownNewAttr::No);
}

// ---------------------------------------------------------------
// Others and helpers

nsDOMCSSDeclaration* nsStyledElement::Style() {
  Element::nsDOMSlots* slots = DOMSlots();

  if (!slots->mStyle) {
    // Just in case...
    ReparseStyleAttribute(/* aForceInDataDoc */ true);

    slots->mStyle = new nsDOMCSSAttributeDeclaration(this, false);
    SetMayHaveStyle();
  }

  return slots->mStyle;
}

StylePropertyMap* nsStyledElement::AttributeStyleMap() {
  nsDOMSlots* slots = DOMSlots();

  if (!slots->mAttributeStyleMap) {
    slots->mAttributeStyleMap = MakeRefPtr<StylePropertyMap>(this);
  }

  return slots->mAttributeStyleMap;
}

nsresult nsStyledElement::ReparseStyleAttribute(bool aForceInDataDoc) {
  if (!MayHaveStyle()) {
    return NS_OK;
  }
  const nsAttrValue* oldVal = mAttrs.GetAttr(nsGkAtoms::style);
  if (oldVal && oldVal->Type() != nsAttrValue::eCSSDeclaration) {
    nsAttrValue attrValue;
    nsAutoString stringValue;
    oldVal->ToString(stringValue);
    if (!stringValue.IsEmpty()) {
      // Prevent CSP errors from re-parsing a style that was already blocked.
      ParseStyleAttribute(stringValue, nullptr, attrValue, aForceInDataDoc);
    }
    // Don't bother going through SetInlineStyleDeclaration; we don't
    // want to fire off mutation events or document notifications anyway
    bool oldValueSet;
    nsresult rv = SetAndSwapAttr(nsGkAtoms::style, attrValue, &oldValueSet,
                                 mozilla::dom::IsKnownNewAttr::No);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsDOMCSSDeclaration* nsStyledElement::GetExistingStyle() {
  Element::nsDOMSlots* slots = GetExistingDOMSlots();
  if (!slots) {
    return nullptr;
  }

  return slots->mStyle;
}

void nsStyledElement::ParseStyleAttribute(const nsAString& aValue,
                                          nsIPrincipal* aMaybeScriptedPrincipal,
                                          nsAttrValue& aResult,
                                          bool aForceInDataDoc) {
  Document* doc = OwnerDoc();
  bool isNativeAnon = IsInNativeAnonymousSubtree();

  if (!isNativeAnon &&
      !nsStyleUtil::CSPAllowsInlineStyle(this, doc, aMaybeScriptedPrincipal, 0,
                                         1, aValue, nullptr)) {
    return;
  }

  if (aForceInDataDoc || !doc->IsLoadedAsData() || GetExistingStyle() ||
      doc->IsStaticDocument()) {
    bool isCSS = true;  // assume CSS until proven otherwise

    if (!isNativeAnon) {  // native anonymous content always assumes CSS
      nsAutoString styleType;
      doc->GetHeaderData(nsGkAtoms::headerContentStyleType, styleType);
      if (!styleType.IsEmpty()) {
        isCSS = StringBeginsWith(styleType, u"text/css"_ns,
                                 nsASCIICaseInsensitiveStringComparator);
      }
    }

    if (isCSS &&
        aResult.ParseStyleAttribute(aValue, aMaybeScriptedPrincipal, this)) {
      return;
    }
  }

  aResult.SetTo(aValue);
}

nsresult nsStyledElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  nsresult rv = nsStyledElementBase::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  if (HasAttr(nsGkAtoms::autofocus) && aContext.AllowsAutoFocus() &&
      (!IsSVGElement() || IsFocusableWithoutStyle())) {
    aContext.OwnerDoc().ElementWithAutoFocusInserted(this);
  }

  return NS_OK;
}
