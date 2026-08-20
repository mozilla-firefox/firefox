/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLBodyElement.h"

#include "DocumentInlines.h"
#include "mozilla/AttributeStyles.h"
#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/TextEditor.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLBodyElementBinding.h"
#include "nsAttrValueInlines.h"
#include "nsDocShell.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowInner.h"
#include "nsIDocShell.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(Body)

namespace mozilla::dom {

//----------------------------------------------------------------------

HTMLBodyElement::~HTMLBodyElement() = default;

JSObject* HTMLBodyElement::WrapNode(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return HTMLBodyElement_Binding::Wrap(aCx, this, aGivenProto);
}

NS_IMPL_ELEMENT_CLONE(HTMLBodyElement)

bool HTMLBodyElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                     const nsAString& aValue,
                                     nsIPrincipal* aMaybeScriptedPrincipal,
                                     nsAttrValue& aResult) {
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::bgcolor || aAttribute == nsGkAtoms::text ||
        aAttribute == nsGkAtoms::link || aAttribute == nsGkAtoms::alink ||
        aAttribute == nsGkAtoms::vlink) {
      return aResult.ParseColor(aValue);
    }
    if (aAttribute == nsGkAtoms::marginwidth ||
        aAttribute == nsGkAtoms::marginheight ||
        aAttribute == nsGkAtoms::topmargin ||
        aAttribute == nsGkAtoms::leftmargin) {
      return aResult.ParseNonNegativeIntValue(aValue);
    }
  }

  return nsGenericHTMLElement::ParseBackgroundAttribute(
             aNamespaceID, aAttribute, aValue, aResult) ||
         nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

void HTMLBodyElement::MapAttributesIntoRule(
    MappedDeclarationsBuilder& aBuilder) {
  // This is the one place where we try to set the same property
  // multiple times in presentation attributes. Servo does not support
  // querying if a property is set (because that is O(n) behavior
  // in ServoSpecifiedValues). Instead, we use the below values to keep
  // track of whether we have already set a property, and if so, what value
  // we set it to (which is used when handling margin
  // attributes from the containing frame element)

  int32_t bodyMarginWidth = -1;
  int32_t bodyMarginHeight = -1;
  int32_t bodyTopMargin = -1;
  int32_t bodyLeftMargin = -1;

  const nsAttrValue* value;
  // if marginwidth/marginheight are set, reflect them as 'margin'
  value = aBuilder.GetAttr(nsGkAtoms::marginwidth);
  if (value && value->Type() == nsAttrValue::eInteger) {
    bodyMarginWidth = value->GetIntegerValue();
    if (bodyMarginWidth < 0) {
      bodyMarginWidth = 0;
    }
    aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_left,
                                  (float)bodyMarginWidth);
    aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_right,
                                  (float)bodyMarginWidth);
  }

  value = aBuilder.GetAttr(nsGkAtoms::marginheight);
  if (value && value->Type() == nsAttrValue::eInteger) {
    bodyMarginHeight = value->GetIntegerValue();
    if (bodyMarginHeight < 0) {
      bodyMarginHeight = 0;
    }
    aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_top,
                                  (float)bodyMarginHeight);
    aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_bottom,
                                  (float)bodyMarginHeight);
  }

  // topmargin (IE-attribute)
  // Sets both margin-top and margin-bottom.
  // See https://github.com/whatwg/html/issues/11879
  if (bodyMarginHeight == -1) {
    value = aBuilder.GetAttr(nsGkAtoms::topmargin);
    if (value && value->Type() == nsAttrValue::eInteger) {
      bodyTopMargin = value->GetIntegerValue();
      if (bodyTopMargin < 0) {
        bodyTopMargin = 0;
      }
      aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_top,
                                    (float)bodyTopMargin);
      aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_bottom,
                                    (float)bodyTopMargin);
    }
  }

  // leftmargin (IE-attribute)
  // Sets both margin-left and margin-right.
  // See https://github.com/whatwg/html/issues/11879
  if (bodyMarginWidth == -1) {
    value = aBuilder.GetAttr(nsGkAtoms::leftmargin);
    if (value && value->Type() == nsAttrValue::eInteger) {
      bodyLeftMargin = value->GetIntegerValue();
      if (bodyLeftMargin < 0) {
        bodyLeftMargin = 0;
      }
      aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_left,
                                    (float)bodyLeftMargin);
      aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_right,
                                    (float)bodyLeftMargin);
    }
  }

  // if marginwidth or marginheight is set in the <frame> and not set in the
  // <body> reflect them as margin in the <body>
  if (bodyMarginWidth == -1 || bodyMarginHeight == -1) {
    if (nsDocShell* ds = nsDocShell::Cast(aBuilder.Document().GetDocShell())) {
      CSSIntSize margins = ds->GetFrameMargins();
      int32_t frameMarginWidth = margins.width;
      int32_t frameMarginHeight = margins.height;

      if (bodyMarginWidth == -1 && frameMarginWidth >= 0) {
        if (bodyLeftMargin == -1) {
          aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_left,
                                        (float)frameMarginWidth);
          aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_right,
                                        (float)frameMarginWidth);
        }
      }

      if (bodyMarginHeight == -1 && frameMarginHeight >= 0) {
        if (bodyTopMargin == -1) {
          aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_top,
                                        (float)frameMarginHeight);
          aBuilder.SetPixelValueIfUnset(eCSSProperty_margin_bottom,
                                        (float)frameMarginHeight);
        }
      }
    }
  }

  // When display if first asked for, go ahead and get our colors set up.
  if (AttributeStyles* attrStyles = aBuilder.Document().GetAttributeStyles()) {
    nscolor color;
    value = aBuilder.GetAttr(nsGkAtoms::link);
    if (value && value->GetColorValue(color)) {
      attrStyles->SetLinkColor(color);
    }

    value = aBuilder.GetAttr(nsGkAtoms::alink);
    if (value && value->GetColorValue(color)) {
      attrStyles->SetActiveLinkColor(color);
    }

    value = aBuilder.GetAttr(nsGkAtoms::vlink);
    if (value && value->GetColorValue(color)) {
      attrStyles->SetVisitedLinkColor(color);
    }
  }

  if (!aBuilder.PropertyIsSet(eCSSProperty_color)) {
    // color: color
    nscolor color;
    value = aBuilder.GetAttr(nsGkAtoms::text);
    if (value && value->GetColorValue(color)) {
      aBuilder.SetColorValue(eCSSProperty_color, color);
    }
  }

  nsGenericHTMLElement::MapBackgroundAttributesInto(aBuilder);
  nsGenericHTMLElement::MapCommonAttributesInto(aBuilder);
}

nsMapRuleToAttributesFunc HTMLBodyElement::GetAttributeMappingFunction() const {
  return &MapAttributesIntoRule;
}

NS_IMETHODIMP_(bool)
HTMLBodyElement::IsAttributeMapped(const nsAtom* aAttribute) const {
  static const MappedAttributeEntry attributes[] = {
      {nsGkAtoms::link},
      {nsGkAtoms::vlink},
      {nsGkAtoms::alink},
      {nsGkAtoms::text},
      {nsGkAtoms::marginwidth},
      {nsGkAtoms::marginheight},
      {nsGkAtoms::topmargin},
      {nsGkAtoms::leftmargin},
      {nullptr},
  };

  static const MappedAttributeEntry* const map[] = {
      attributes,
      sCommonAttributeMap,
      sBackgroundAttributeMap,
  };

  return FindAttributeDependence(aAttribute, map);
}

bool HTMLBodyElement::IsEventAttributeNameInternal(nsAtom* aName) {
  return nsContentUtils::IsEventAttributeName(
      aName, EventNameType_HTML | EventNameType_HTMLBodyOrFramesetOnly);
}

nsresult HTMLBodyElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  mAttrs.MarkAsPendingPresAttributeEvaluation();
  return nsGenericHTMLElement::BindToTree(aContext, aParent);
}

void HTMLBodyElement::FrameMarginsChanged() {
  MOZ_ASSERT(IsInComposedDoc());
  if (IsPendingMappedAttributeEvaluation()) {
    return;
  }
  if (mAttrs.MarkAsPendingPresAttributeEvaluation()) {
    OwnerDoc()->ScheduleForPresAttrEvaluation(this);
  }
}

#define EVENT(name_, id_, type_, \
              struct_) /* nothing; handled by the superclass */
// nsGenericHTMLElement::GetOnError returns
// already_AddRefed<EventHandlerNonNull> while other getters return
// EventHandlerNonNull*, so allow passing in the type to use here.
#define WINDOW_EVENT_HELPER(name_, type_)                              \
  type_* HTMLBodyElement::GetOn##name_() {                             \
    if (nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow()) {      \
      nsGlobalWindowInner* globalWin = nsGlobalWindowInner::Cast(win); \
      return globalWin->GetOn##name_();                                \
    }                                                                  \
    return nullptr;                                                    \
  }                                                                    \
  void HTMLBodyElement::SetOn##name_(type_* handler) {                 \
    nsPIDOMWindowInner* win = OwnerDoc()->GetInnerWindow();            \
    if (!win) {                                                        \
      return;                                                          \
    }                                                                  \
                                                                       \
    nsGlobalWindowInner* globalWin = nsGlobalWindowInner::Cast(win);   \
    return globalWin->SetOn##name_(handler);                           \
  }
#define WINDOW_EVENT(name_, id_, type_, struct_) \
  WINDOW_EVENT_HELPER(name_, EventHandlerNonNull)
#define BEFOREUNLOAD_EVENT(name_, id_, type_, struct_) \
  WINDOW_EVENT_HELPER(name_, OnBeforeUnloadEventHandlerNonNull)
#include "mozilla/EventNameList.inc"  // IWYU pragma: keep
#undef BEFOREUNLOAD_EVENT
#undef WINDOW_EVENT
#undef WINDOW_EVENT_HELPER
#undef EVENT

}  // namespace mozilla::dom
