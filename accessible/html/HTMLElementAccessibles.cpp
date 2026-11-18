/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLElementAccessibles.h"

#include "CacheConstants.h"
#include "Relation.h"
#include "States.h"
#include "mozilla/a11y/Role.h"
#include "mozilla/dom/HTMLDetailsElement.h"
#include "mozilla/dom/HTMLLabelElement.h"
#include "mozilla/dom/HTMLSummaryElement.h"
#include "nsCoreUtils.h"
#include "nsTextEquivUtils.h"

using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// HTMLHRAccessible
////////////////////////////////////////////////////////////////////////////////

role HTMLHRAccessible::NativeRole() const { return roles::SEPARATOR; }

////////////////////////////////////////////////////////////////////////////////
// HTMLBRAccessible
////////////////////////////////////////////////////////////////////////////////

role HTMLBRAccessible::NativeRole() const { return roles::WHITESPACE; }

uint64_t HTMLBRAccessible::NativeState() const { return states::READONLY; }

ENameValueFlag HTMLBRAccessible::NativeName(nsString& aName) const {
  aName = static_cast<char16_t>('\n');  // Newline char
  return eNameOK;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLLabelAccessible
////////////////////////////////////////////////////////////////////////////////

ENameValueFlag HTMLLabelAccessible::NativeName(nsString& /*aName*/) const {
  return eNameOK;
}

Relation HTMLLabelAccessible::RelationByType(RelationType aType) const {
  Relation rel = AccessibleWrap::RelationByType(aType);
  if (aType == RelationType::LABEL_FOR) {
    dom::HTMLLabelElement* label = dom::HTMLLabelElement::FromNode(mContent);
    rel.AppendTarget(mDoc, label->GetLabeledElementInternal());
  }

  return rel;
}

void HTMLLabelAccessible::DOMAttributeChanged(int32_t aNameSpaceID,
                                              nsAtom* aAttribute,
                                              AttrModType aModType,
                                              const nsAttrValue* aOldValue,
                                              uint64_t aOldState) {
  HyperTextAccessible::DOMAttributeChanged(aNameSpaceID, aAttribute, aModType,
                                           aOldValue, aOldState);

  if (aAttribute == nsGkAtoms::_for) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations | CacheDomain::Actions);
  }
}

bool HTMLLabelAccessible::HasPrimaryAction() const {
  return nsCoreUtils::IsLabelWithControl(mContent);
}

void HTMLLabelAccessible::ActionNameAt(uint8_t aIndex, nsAString& aName) {
  if (aIndex == 0) {
    if (HasPrimaryAction()) {
      aName.AssignLiteral("click");
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLOuputAccessible
////////////////////////////////////////////////////////////////////////////////

Relation HTMLOutputAccessible::RelationByType(RelationType aType) const {
  Relation rel = AccessibleWrap::RelationByType(aType);
  if (aType == RelationType::CONTROLLED_BY) {
    rel.AppendIter(
        new AssociatedElementsIterator(mDoc, mContent, nsGkAtoms::_for));
  }

  return rel;
}

void HTMLOutputAccessible::DOMAttributeChanged(int32_t aNameSpaceID,
                                               nsAtom* aAttribute,
                                               AttrModType aModType,
                                               const nsAttrValue* aOldValue,
                                               uint64_t aOldState) {
  HyperTextAccessible::DOMAttributeChanged(aNameSpaceID, aAttribute, aModType,
                                           aOldValue, aOldState);

  if (aAttribute == nsGkAtoms::_for) {
    mDoc->QueueCacheUpdate(this, CacheDomain::Relations);
  }
}

////////////////////////////////////////////////////////////////////////////////
// HTMLSummaryAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLSummaryAccessible::HTMLSummaryAccessible(nsIContent* aContent,
                                             DocAccessible* aDoc)
    : HyperTextAccessible(aContent, aDoc) {
  mGenericTypes |= eButton;
}

bool HTMLSummaryAccessible::HasPrimaryAction() const { return true; }

void HTMLSummaryAccessible::ActionNameAt(uint8_t aIndex, nsAString& aName) {
  if (aIndex != eAction_Click) {
    return;
  }

  dom::HTMLSummaryElement* summary =
      dom::HTMLSummaryElement::FromNode(mContent);
  if (!summary) {
    return;
  }

  dom::HTMLDetailsElement* details = summary->GetDetails();
  if (!details) {
    return;
  }

  if (details->Open()) {
    aName.AssignLiteral("collapse");
  } else {
    aName.AssignLiteral("expand");
  }
}

uint64_t HTMLSummaryAccessible::NativeState() const {
  uint64_t state = HyperTextAccessible::NativeState();

  dom::HTMLSummaryElement* summary =
      dom::HTMLSummaryElement::FromNode(mContent);
  if (!summary) {
    return state;
  }

  dom::HTMLDetailsElement* details = summary->GetDetails();
  if (!details) {
    return state;
  }

  state |= states::EXPANDABLE;

  if (details->Open()) {
    state |= states::EXPANDED;
  }

  return state;
}

HTMLSummaryAccessible* HTMLSummaryAccessible::FromDetails(
    LocalAccessible* details) {
  if (!dom::HTMLDetailsElement::FromNodeOrNull(details->GetContent())) {
    return nullptr;
  }

  HTMLSummaryAccessible* summaryAccessible = nullptr;
  for (uint32_t i = 0; i < details->ChildCount(); i++) {
    // Iterate through the children of our details accessible to locate main
    // summary. This iteration includes the anonymous summary if the details
    // element was not explicitly created with one.
    LocalAccessible* child = details->LocalChildAt(i);
    auto* summary =
        mozilla::dom::HTMLSummaryElement::FromNodeOrNull(child->GetContent());
    if (summary && summary->IsMainSummary()) {
      summaryAccessible = static_cast<HTMLSummaryAccessible*>(child);
      break;
    }
  }

  return summaryAccessible;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLSummaryAccessible: Widgets

bool HTMLSummaryAccessible::IsWidget() const { return true; }

////////////////////////////////////////////////////////////////////////////////
// HTMLHeaderOrFooterAccessible
////////////////////////////////////////////////////////////////////////////////

role HTMLHeaderOrFooterAccessible::NativeRole() const {
  // Only map header and footer if they are direct descendants of the body tag.
  // If other sectioning or sectioning root elements, they become sections.
  nsIContent* parent = mContent->GetParent();
  while (parent) {
    if (parent->IsAnyOfHTMLElements(
            nsGkAtoms::article, nsGkAtoms::aside, nsGkAtoms::nav,
            nsGkAtoms::section, nsGkAtoms::main, nsGkAtoms::blockquote,
            nsGkAtoms::details, nsGkAtoms::dialog, nsGkAtoms::fieldset,
            nsGkAtoms::figure, nsGkAtoms::td)) {
      break;
    }
    parent = parent->GetParent();
  }

  // No sectioning or sectioning root elements found.
  if (!parent) {
    return roles::LANDMARK;
  }

  return roles::SECTION;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLAsideAccessible
////////////////////////////////////////////////////////////////////////////////

role HTMLAsideAccessible::NativeRole() const {
  // Per the HTML-AAM spec, there are two cases for aside elements:
  //   1. scoped to body or main elements -> 'complementary' role
  //   2. scoped to sectioning content elements
  //       -> if the element has an accessible name, 'complementary' role
  //       -> otherwise, 'generic' role
  // To implement this, walk ancestors until we find a sectioning content
  // element, or a body/main element, then take actions based on the rules
  // above.
  nsIContent* parent = mContent->GetParent();
  while (parent) {
    if (parent->IsAnyOfHTMLElements(nsGkAtoms::article, nsGkAtoms::aside,
                                    nsGkAtoms::nav, nsGkAtoms::section)) {
      return !NameIsEmpty() ? roles::LANDMARK : roles::SECTION;
    }
    if (parent->IsAnyOfHTMLElements(nsGkAtoms::main, nsGkAtoms::body)) {
      return roles::LANDMARK;
    }
    parent = parent->GetParent();
  }

  // Fall back to landmark, though we always expect to find a body element.
  return roles::LANDMARK;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLSectionAccessible
////////////////////////////////////////////////////////////////////////////////

role HTMLSectionAccessible::NativeRole() const {
  return NameIsEmpty() ? roles::SECTION : roles::REGION;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLAbbreviationAccessible
////////////////////////////////////////////////////////////////////////////////

ENameValueFlag HTMLAbbreviationAccessible::NativeName(nsString& aName) const {
  if (mContent->AsElement()->GetAttr(nsGkAtoms::title, aName)) {
    // "title" tag takes priority
    return eNameOK;
  }

  return HyperTextAccessible::NativeName(aName);
}

void HTMLAbbreviationAccessible::DOMAttributeChanged(
    int32_t aNameSpaceID, nsAtom* aAttribute, AttrModType aModType,
    const nsAttrValue* aOldValue, uint64_t aOldState) {
  if (aAttribute == nsGkAtoms::title) {
    nsAutoString name;
    ARIAName(name);
    if (name.IsEmpty()) {
      mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, this);
      return;
    }

    if (!mContent->AsElement()->HasAttr(nsGkAtoms::aria_describedby)) {
      mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE,
                             this);
    }

    return;
  }

  HyperTextAccessible::DOMAttributeChanged(aNameSpaceID, aAttribute, aModType,
                                           aOldValue, aOldState);
}
