/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLSelectAccessible.h"

#include "DocAccessible-inl.h"
#include "DocAccessible.h"
#include "LocalAccessible-inl.h"
#include "States.h"
#include "mozilla/a11y/Role.h"
#include "mozilla/dom/HTMLOptGroupElement.h"
#include "mozilla/dom/HTMLOptionElement.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "nsAccUtils.h"
#include "nsAccessibilityService.h"
#include "nsCOMPtr.h"
#include "nsComboboxControlFrame.h"
#include "nsContainerFrame.h"
#include "nsListControlFrame.h"

using namespace mozilla::a11y;
using namespace mozilla::dom;

////////////////////////////////////////////////////////////////////////////////
// HTMLSelectListAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLSelectListAccessible::HTMLSelectListAccessible(nsIContent* aContent,
                                                   DocAccessible* aDoc)
    : AccessibleWrap(aContent, aDoc) {
  mGenericTypes |= eListControl | eSelect;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLSelectListAccessible: LocalAccessible public

uint64_t HTMLSelectListAccessible::NativeState() const {
  uint64_t state = AccessibleWrap::NativeState();
  if (mContent->AsElement()->HasAttr(nsGkAtoms::multiple)) {
    state |= states::MULTISELECTABLE | states::EXTSELECTABLE;
  }

  return state;
}

role HTMLSelectListAccessible::NativeRole() const { return roles::LISTBOX; }

////////////////////////////////////////////////////////////////////////////////
// HTMLSelectListAccessible: SelectAccessible

bool HTMLSelectListAccessible::SelectAll() {
  return mContent->AsElement()->HasAttr(nsGkAtoms::multiple)
             ? AccessibleWrap::SelectAll()
             : false;
}

bool HTMLSelectListAccessible::UnselectAll() {
  return mContent->AsElement()->HasAttr(nsGkAtoms::multiple)
             ? AccessibleWrap::UnselectAll()
             : false;
}

void HTMLSelectListAccessible::ActionNameAt(uint8_t aIndex, nsAString& aName) {
  if (aIndex == eAction_Click) aName.AssignLiteral("click");
}

////////////////////////////////////////////////////////////////////////////////
// HTMLSelectListAccessible: Widgets

bool HTMLSelectListAccessible::IsWidget() const { return true; }

bool HTMLSelectListAccessible::IsActiveWidget() const {
  return FocusMgr()->HasDOMFocus(mContent);
}

bool HTMLSelectListAccessible::AreItemsOperable() const { return true; }

LocalAccessible* HTMLSelectListAccessible::CurrentItem() const {
  nsListControlFrame* listControlFrame = do_QueryFrame(GetFrame());
  if (listControlFrame) {
    nsCOMPtr<nsIContent> activeOptionNode =
        listControlFrame->GetCurrentOption();
    if (activeOptionNode) {
      DocAccessible* document = Document();
      if (document) return document->GetAccessible(activeOptionNode);
    }
  }
  return nullptr;
}

void HTMLSelectListAccessible::SetCurrentItem(const LocalAccessible* aItem) {
  if (!aItem->GetContent()->IsElement()) {
    return;
  }
  aItem->GetContent()->AsElement()->SetAttr(kNameSpaceID_None,
                                            nsGkAtoms::selected, u"true"_ns,
                                            true, IsKnownNewAttr::No);
}

bool HTMLSelectListAccessible::IsAcceptableChild(nsIContent* aEl) const {
  return aEl->IsAnyOfHTMLElements(nsGkAtoms::option, nsGkAtoms::optgroup);
}

bool HTMLSelectListAccessible::AttributeChangesState(nsAtom* aAttribute) {
  return aAttribute == nsGkAtoms::multiple ||
         LocalAccessible::AttributeChangesState(aAttribute);
}

////////////////////////////////////////////////////////////////////////////////
// HTMLSelectOptionAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLSelectOptionAccessible::HTMLSelectOptionAccessible(nsIContent* aContent,
                                                       DocAccessible* aDoc)
    : HyperTextAccessible(aContent, aDoc) {}

////////////////////////////////////////////////////////////////////////////////
// HTMLSelectOptionAccessible: LocalAccessible public

role HTMLSelectOptionAccessible::NativeRole() const {
  if (GetCombobox()) return roles::COMBOBOX_OPTION;

  return roles::OPTION;
}

ENameValueFlag HTMLSelectOptionAccessible::NativeName(nsString& aName) const {
  if (auto* option = dom::HTMLOptionElement::FromNode(mContent)) {
    option->GetAttr(nsGkAtoms::label, aName);
    if (!aName.IsEmpty()) {
      return eNameOK;
    }
    option->GetText(aName);
    return eNameFromSubtree;
  }
  if (auto* group = dom::HTMLOptGroupElement::FromNode(mContent)) {
    group->GetLabel(aName);
    return aName.IsEmpty() ? eNameOK : eNameFromSubtree;
  }
  MOZ_ASSERT_UNREACHABLE("What content do we have?");
  return eNameFromSubtree;
}

void HTMLSelectOptionAccessible::DOMAttributeChanged(
    int32_t aNameSpaceID, nsAtom* aAttribute, AttrModType aModType,
    const nsAttrValue* aOldValue, uint64_t aOldState) {
  HyperTextAccessible::DOMAttributeChanged(aNameSpaceID, aAttribute, aModType,
                                           aOldValue, aOldState);

  if (aAttribute == nsGkAtoms::label) {
    dom::Element* elm = Elm();
    if (!nsAccUtils::HasARIAAttr(elm, nsGkAtoms::aria_labelledby) &&
        !nsAccUtils::HasARIAAttr(elm, nsGkAtoms::aria_label)) {
      mDoc->FireDelayedEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, this);
    }
  }
}

uint64_t HTMLSelectOptionAccessible::NativeState() const {
  // As a HTMLSelectOptionAccessible we can have the following states:
  // SELECTABLE, SELECTED, FOCUSED, FOCUSABLE, OFFSCREEN
  // Upcall to LocalAccessible, but skip HyperTextAccessible impl
  // because we don't want EDITABLE or SELECTABLE_TEXT
  uint64_t state = LocalAccessible::NativeState();

  LocalAccessible* select = GetSelect();
  if (!select) return state;

  uint64_t selectState = select->State();
  if (selectState & states::INVISIBLE) return state;

  // Are we selected?
  HTMLOptionElement* option = HTMLOptionElement::FromNode(mContent);
  bool selected = option && option->Selected();
  if (selected) state |= states::SELECTED;

  if (selectState & states::OFFSCREEN) {
    state |= states::OFFSCREEN;
  } else if (selectState & states::COLLAPSED) {
    // <select> is COLLAPSED: add OFFSCREEN, if not the currently
    // visible option
    if (!selected) {
      state |= states::OFFSCREEN;
      // Ensure the invisible state is removed. Otherwise, group info will skip
      // this option. Furthermore, this gets cached and this doesn't get
      // invalidated even once the select is expanded.
      state &= ~states::INVISIBLE;
    } else {
      // Clear offscreen and invisible for currently showing option
      state &= ~(states::OFFSCREEN | states::INVISIBLE);
      state |= selectState & states::OPAQUE1;
    }
  } else {
    // XXX list frames are weird, don't rely on LocalAccessible's general
    // visibility implementation unless they get reimplemented in layout
    state &= ~states::OFFSCREEN;
    // <select> is not collapsed: compare bounds to calculate OFFSCREEN
    LocalAccessible* listAcc = LocalParent();
    if (listAcc) {
      LayoutDeviceIntRect optionRect = Bounds();
      LayoutDeviceIntRect listRect = listAcc->Bounds();
      if (optionRect.Y() < listRect.Y() ||
          optionRect.YMost() > listRect.YMost()) {
        state |= states::OFFSCREEN;
      }
    }
  }

  return state;
}

uint64_t HTMLSelectOptionAccessible::NativeInteractiveState() const {
  return NativelyUnavailable() ? states::UNAVAILABLE
                               : states::FOCUSABLE | states::SELECTABLE;
}

nsRect HTMLSelectOptionAccessible::RelativeBounds(
    nsIFrame** aBoundingFrame) const {
  LocalAccessible* combobox = GetCombobox();
  if (combobox && (combobox->State() & states::COLLAPSED)) {
    return combobox->RelativeBounds(aBoundingFrame);
  }

  return HyperTextAccessible::RelativeBounds(aBoundingFrame);
}

void HTMLSelectOptionAccessible::ActionNameAt(uint8_t aIndex,
                                              nsAString& aName) {
  if (aIndex == eAction_Select) aName.AssignLiteral("select");
}

bool HTMLSelectOptionAccessible::HasPrimaryAction() const { return true; }

void HTMLSelectOptionAccessible::SetSelected(bool aSelect) {
  HTMLOptionElement* option = HTMLOptionElement::FromNode(mContent);
  if (option) option->SetSelected(aSelect);
}

////////////////////////////////////////////////////////////////////////////////
// HTMLSelectOptionAccessible: Widgets

LocalAccessible* HTMLSelectOptionAccessible::ContainerWidget() const {
  LocalAccessible* parent = LocalParent();
  if (parent && parent->IsHTMLOptGroup()) {
    parent = parent->LocalParent();
  }

  return parent && parent->IsListControl() ? parent : nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLSelectOptGroupAccessible
////////////////////////////////////////////////////////////////////////////////

role HTMLSelectOptGroupAccessible::NativeRole() const {
  return roles::GROUPING;
}

uint64_t HTMLSelectOptGroupAccessible::NativeInteractiveState() const {
  return NativelyUnavailable() ? states::UNAVAILABLE : 0;
}

bool HTMLSelectOptGroupAccessible::IsAcceptableChild(nsIContent* aEl) const {
  return aEl->IsCharacterData() || aEl->IsHTMLElement(nsGkAtoms::option);
}

bool HTMLSelectOptGroupAccessible::HasPrimaryAction() const { return false; }

////////////////////////////////////////////////////////////////////////////////
// HTMLComboboxAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLComboboxAccessible::HTMLComboboxAccessible(nsIContent* aContent,
                                               DocAccessible* aDoc)
    : AccessibleWrap(aContent, aDoc) {
  mType = eHTMLComboboxType;
  mGenericTypes |= eCombobox;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLComboboxAccessible: LocalAccessible

role HTMLComboboxAccessible::NativeRole() const { return roles::COMBOBOX; }

uint64_t HTMLComboboxAccessible::NativeState() const {
  // As a HTMLComboboxAccessible we can have the following states:
  // FOCUSED, FOCUSABLE, HASPOPUP, EXPANDED, COLLAPSED
  // Get focus status from base class
  uint64_t state = LocalAccessible::NativeState();

  nsComboboxControlFrame* comboFrame = do_QueryFrame(GetFrame());
  if (comboFrame && comboFrame->IsDroppedDown()) {
    state |= states::EXPANDED;
  }

  state |= states::HASPOPUP | states::EXPANDABLE;
  return state;
}

EDescriptionValueFlag HTMLComboboxAccessible::Description(
    nsString& aDescription) const {
  aDescription.Truncate();
  // First check to see if combo box itself has a description, perhaps through
  // tooltip (title attribute) or via aria-describedby
  EDescriptionValueFlag descFlag = LocalAccessible::Description(aDescription);
  if (!aDescription.IsEmpty()) {
    return descFlag;
  }

  // Otherwise use description of selected option.
  LocalAccessible* option = SelectedOption();
  if (option) {
    return option->Description(aDescription);
  }

  return eDescriptionOK;
}

void HTMLComboboxAccessible::Value(nsString& aValue) const {
  // Use accessible name of selected option.
  LocalAccessible* option = SelectedOption();
  if (option) option->Name(aValue);
}

bool HTMLComboboxAccessible::HasPrimaryAction() const { return true; }

void HTMLComboboxAccessible::ActionNameAt(uint8_t aIndex, nsAString& aName) {
  if (aIndex != HTMLComboboxAccessible::eAction_Click) return;

  nsComboboxControlFrame* comboFrame = do_QueryFrame(GetFrame());
  if (!comboFrame) return;

  if (comboFrame->IsDroppedDown()) {
    aName.AssignLiteral("close");
  } else {
    aName.AssignLiteral("open");
  }
}

bool HTMLComboboxAccessible::IsAcceptableChild(nsIContent* aEl) const {
  return aEl->IsElement() &&
         aEl->AsElement()->GetPseudoElementType() == PseudoStyleType::Picker;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLComboboxAccessible: Widgets

bool HTMLComboboxAccessible::IsWidget() const { return true; }

bool HTMLComboboxAccessible::IsActiveWidget() const {
  return FocusMgr()->HasDOMFocus(mContent);
}

bool HTMLComboboxAccessible::AreItemsOperable() const {
  nsComboboxControlFrame* comboboxFrame = do_QueryFrame(GetFrame());
  return comboboxFrame && comboboxFrame->IsDroppedDown();
}

LocalAccessible* HTMLComboboxAccessible::CurrentItem() const {
  if (AreItemsOperable()) {
    if (HTMLComboboxListAccessible* list = List()) {
      return list->CurrentItem();
    }
  }
  return nullptr;
}

void HTMLComboboxAccessible::SetCurrentItem(const LocalAccessible* aItem) {
  if (AreItemsOperable()) {
    if (HTMLComboboxListAccessible* list = List()) {
      list->SetCurrentItem(aItem);
    }
  }
}

HTMLComboboxListAccessible* HTMLComboboxAccessible::List() const {
  LocalAccessible* firstChild = ChildCount() == 1 ? LocalFirstChild() : nullptr;
  return firstChild && firstChild->IsListControl()
             ? static_cast<HTMLComboboxListAccessible*>(firstChild)
             : nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLComboboxAccessible: protected

LocalAccessible* HTMLComboboxAccessible::SelectedOption() const {
  HTMLSelectElement* select = HTMLSelectElement::FromNode(mContent);
  int32_t selectedIndex = select->SelectedIndex();

  if (selectedIndex >= 0) {
    HTMLOptionElement* option = select->Item(selectedIndex);
    if (option) {
      DocAccessible* document = Document();
      if (document) return document->GetAccessible(option);
    }
  }

  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLComboboxListAccessible
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// HTMLComboboxAccessible: LocalAccessible

role HTMLComboboxListAccessible::NativeRole() const {
  return roles::COMBOBOX_LIST;
}

uint64_t HTMLComboboxListAccessible::NativeState() const {
  // As a HTMLComboboxListAccessible we can have the following states:
  // FOCUSED, FOCUSABLE, FLOATING, INVISIBLE
  // Get focus status from base class
  uint64_t state = LocalAccessible::NativeState();

  LocalAccessible* comboAcc = LocalParent();
  if (comboAcc->State() & states::EXPANDED) {
    state |= states::FLOATING;
  } else {
    state |= states::INVISIBLE;
  }

  return state;
}

nsRect HTMLComboboxListAccessible::RelativeBounds(
    nsIFrame** aBoundingFrame) const {
  *aBoundingFrame = nullptr;

  LocalAccessible* comboAcc = LocalParent();
  if (!comboAcc) return nsRect();

  if (0 == (comboAcc->State() & states::COLLAPSED)) {
    return HTMLSelectListAccessible::RelativeBounds(aBoundingFrame);
  }

  // Get the first option.
  nsIContent* content = mContent->GetFirstChild();
  if (!content) return nsRect();

  nsIFrame* frame = content->GetPrimaryFrame();
  if (!frame) {
    *aBoundingFrame = nullptr;
    return nsRect();
  }

  *aBoundingFrame = frame->GetParent();
  return (*aBoundingFrame)->GetRect();
}

bool HTMLComboboxListAccessible::IsAcceptableChild(nsIContent* aEl) const {
  return aEl->IsAnyOfHTMLElements(nsGkAtoms::option, nsGkAtoms::optgroup);
}

////////////////////////////////////////////////////////////////////////////////
// HTMLComboboxListAccessible: Widgets

bool HTMLComboboxListAccessible::IsActiveWidget() const {
  return mParent && mParent->IsActiveWidget();
}

bool HTMLComboboxListAccessible::AreItemsOperable() const {
  return mParent && mParent->AreItemsOperable();
}
