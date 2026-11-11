/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLHeadingElement.h"

#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLHeadingElementBinding.h"
#include "nsGkAtoms.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(Heading)

namespace mozilla::dom {

HTMLHeadingElement::~HTMLHeadingElement() = default;

NS_IMPL_ELEMENT_CLONE(HTMLHeadingElement)

JSObject* HTMLHeadingElement::WrapNode(JSContext* aCx,
                                       JS::Handle<JSObject*> aGivenProto) {
  return HTMLHeadingElement_Binding::Wrap(aCx, this, aGivenProto);
}

bool HTMLHeadingElement::ParseAttribute(int32_t aNamespaceID,
                                        nsAtom* aAttribute,
                                        const nsAString& aValue,
                                        nsIPrincipal* aMaybeScriptedPrincipal,
                                        nsAttrValue& aResult) {
  if (aAttribute == nsGkAtoms::align && aNamespaceID == kNameSpaceID_None) {
    return ParseDivAlignValue(aValue, aResult);
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aMaybeScriptedPrincipal, aResult);
}

// https://html.spec.whatwg.org/#get-an-element's-computed-heading-offset
uint32_t HTMLHeadingElement::ComputedLevel() const {
  nsAtom* name = NodeInfo()->NameAtom();

  // 1. Let level be 0.
  uint32_t level = 0;

  // XXX: We also compute max to skip unecessary tree traversals
  uint32_t max = 0;

  // 2. If element's local name is h1, then set level to 1.
  if (name == nsGkAtoms::h1) {
    level = 1;
    max = 8;
  }

  // 3. If element's local name is h2, then set level to 2.
  else if (name == nsGkAtoms::h2) {
    level = 2;
    max = 7;
  }

  // 4. If element's local name is h3, then set level to 3.
  else if (name == nsGkAtoms::h3) {
    level = 3;
    max = 6;
  }

  // 5. If element's local name is h4, then set level to 4.
  else if (name == nsGkAtoms::h4) {
    level = 4;
    max = 5;
  }

  // 6. If element's local name is h5, then set level to 5.
  else if (name == nsGkAtoms::h5) {
    level = 5;
    max = 4;
  }

  // 7. If element's local name is h6, then set level to 6.
  else if (name == nsGkAtoms::h6) {
    level = 6;
    max = 3;
  }

  // 8. Assert: level is not zero.
  MOZ_ASSERT(level != 0);
  MOZ_ASSERT(max != 0);

  // 9. Increment level by the result of getting an element's computed heading
  // offset given element.
  if (StaticPrefs::dom_headingoffset_enabled()) {
    level += GetComputedHeadingOffset(max);
  }

  // 10. If level is greater than 9, then return 9.
  // 11. Return level.
  return level > 9 ? 9 : level;
}

// https://html.spec.whatwg.org/#get-an-element's-computed-heading-offset
uint32_t HTMLHeadingElement::GetComputedHeadingOffset(uint32_t aMax) const {
  // 1. Let offset be 0.
  uint32_t offset = 0;

  // 2. Let inclusiveAncestor be element.
  const Element* inclusiveAncestor = this;

  // 3. While inclusiveAncestor is not null:
  while (inclusiveAncestor) {
    const auto* element =
        nsGenericHTMLElement::FromNodeOrNull(inclusiveAncestor);

    // 3.1. Let nextOffset be 0.
    // 3.2. If inclusiveAncestor is an HTML element and has a
    //      headingoffset attribute, then parse its value using the rules for
    //      parsing non-negative integers.
    //
    //      If the result of parsing the value is not an error, then set
    //      nextOffset to that value.
    // 3.3. Increment offset by nextOffset.
    if (element) {
      offset += element->HeadingOffset();
    }

    /// XXX: We can return at this point and prevent tree traversals if we've
    /// already accumulated the max possible value
    if (offset >= aMax) {
      return aMax;
    }

    // 3.4. If inclusiveAncestor is an HTML element and has a headingreset
    // attribute, then return offset.
    if (element && element->HeadingReset()) {
      return offset;
    }

    // 3.5. Set inclusiveAncestor to the parent node of inclusiveAncestor within
    // the composed tree.
    inclusiveAncestor = inclusiveAncestor->GetParentElementCrossingShadowRoot();
  }

  // 4. Return offset.
  return offset;
}

void HTMLHeadingElement::UpdateLevel(bool aNotify) {
  AutoStateChangeNotifier notifier(*this, aNotify);
  RemoveStatesSilently(ElementState::HEADING_LEVEL_BITS);
  uint64_t level = ComputedLevel();

  // ElementState has 4 bits for the heading level, but they are not the LMB,
  // so we need to shift the given level up to those bits.
  MOZ_ASSERT(level > 0 && level < 16, "ComputedLevel() must fit into 4 bits!");
  uint64_t bits = (level << HEADING_LEVEL_OFFSET);
  MOZ_ASSERT((bits & ElementState::HEADING_LEVEL_BITS.bits) == bits);

  AddStatesSilently(ElementState(bits));
}

void HTMLHeadingElement::MapAttributesIntoRule(
    MappedDeclarationsBuilder& aBuilder) {
  nsGenericHTMLElement::MapDivAlignAttributeInto(aBuilder);
  nsGenericHTMLElement::MapCommonAttributesInto(aBuilder);
}

bool HTMLHeadingElement::IsNoNamespaceAttrMapped(
    const nsAtom* aAttribute) const {
  static const MappedAttributeEntry* const map[] = {sDivAlignAttributeMap,
                                                    sCommonAttributeMap};

  return FindAttributeDependence(aAttribute, map);
}

nsMapRuleToAttributesFunc HTMLHeadingElement::GetAttributeMappingFunction()
    const {
  return &MapAttributesIntoRule;
}

}  // namespace mozilla::dom
