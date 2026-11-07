/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11y_HTMLSelectAccessible_h_
#define mozilla_a11y_HTMLSelectAccessible_h_

#include "HTMLFormControlAccessible.h"

namespace mozilla {
namespace a11y {

/**
 *  Selects, Listboxes and Comboboxes, are made up of a number of different
 *  widgets, some of which are shared between the two. This file contains
 *  all of the widgets for both of the Selects, for HTML only.
 *
 *  Listbox:
 *     - HTMLSelectListAccessible
 *        - HTMLSelectOptionAccessible
 *
 *  Comboboxes:
 *     - HTMLComboboxAccessible
 *        - HTMLComboboxListAccessible  [ inserted in accessible tree ]
 *           - HTMLSelectOptionAccessible(s)
 */

/*
 * The list that contains all the options in the select.
 */
class HTMLSelectListAccessible : public AccessibleWrap {
 public:
  enum { eAction_Click = 0 };

  HTMLSelectListAccessible(nsIContent* aContent, DocAccessible* aDoc);
  virtual ~HTMLSelectListAccessible() = default;

  // LocalAccessible
  a11y::role NativeRole() const override;
  uint64_t NativeState() const override;
  bool IsAcceptableChild(nsIContent* aEl) const override;
  bool AttributeChangesState(nsAtom* aAttribute) override;

  // SelectAccessible
  bool SelectAll() override;
  bool UnselectAll() override;

  // ActionAccessible
  bool HasPrimaryAction() const override { return true; }
  void ActionNameAt(uint8_t aIndex, nsAString& aName) override;

  // Widgets
  bool IsWidget() const override;
  bool IsActiveWidget() const override;
  bool AreItemsOperable() const override;
  LocalAccessible* CurrentItem() const override;
  void SetCurrentItem(const LocalAccessible* aItem) override;
};

/*
 * Options inside the select, contained within the list
 */
class HTMLSelectOptionAccessible : public HyperTextAccessible {
 public:
  enum { eAction_Select = 0 };

  HTMLSelectOptionAccessible(nsIContent* aContent, DocAccessible* aDoc);
  virtual ~HTMLSelectOptionAccessible() = default;

  // LocalAccessible
  virtual a11y::role NativeRole() const override;
  virtual uint64_t NativeState() const override;
  virtual uint64_t NativeInteractiveState() const override;

  virtual nsRect RelativeBounds(nsIFrame** aBoundingFrame) const override;
  virtual void SetSelected(bool aSelect) override;

  // ActionAccessible
  virtual bool HasPrimaryAction() const override;
  virtual void ActionNameAt(uint8_t aIndex, nsAString& aName) override;

  // Widgets
  virtual LocalAccessible* ContainerWidget() const override;

 protected:
  // LocalAccessible
  virtual ENameValueFlag NativeName(nsString& aName) const override;
  virtual void DOMAttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                                   AttrModType aModType,
                                   const nsAttrValue* aOldValue,
                                   uint64_t aOldState) override;

 private:
  /**
   * Return a select accessible the option belongs to if any.
   */
  LocalAccessible* GetSelect() const {
    LocalAccessible* parent = mParent;
    if (parent && parent->IsHTMLOptGroup()) {
      parent = parent->LocalParent();
    }

    if (parent && parent->IsListControl()) {
      LocalAccessible* combobox = parent->LocalParent();
      return combobox && combobox->IsCombobox() ? combobox : mParent;
    }

    return nullptr;
  }

  /**
   * Return a combobox accessible the option belongs to if any.
   */
  LocalAccessible* GetCombobox() const {
    LocalAccessible* parent = mParent;
    if (parent && parent->IsHTMLOptGroup()) {
      parent = parent->LocalParent();
    }

    if (parent && parent->IsListControl()) {
      LocalAccessible* combobox = parent->LocalParent();
      return combobox && combobox->IsCombobox() ? combobox : nullptr;
    }

    return nullptr;
  }
};

/*
 * Opt Groups inside the select, contained within the list
 */
class HTMLSelectOptGroupAccessible : public HTMLSelectOptionAccessible {
 public:
  HTMLSelectOptGroupAccessible(nsIContent* aContent, DocAccessible* aDoc)
      : HTMLSelectOptionAccessible(aContent, aDoc) {
    mType = eHTMLOptGroupType;
  }
  virtual ~HTMLSelectOptGroupAccessible() = default;

  // LocalAccessible
  virtual a11y::role NativeRole() const override;
  virtual uint64_t NativeInteractiveState() const override;
  virtual bool IsAcceptableChild(nsIContent* aEl) const override;

  // ActionAccessible
  virtual bool HasPrimaryAction() const override;
};

/** ------------------------------------------------------ */
/**  Finally, the Combobox widgets                         */
/** ------------------------------------------------------ */

class HTMLComboboxListAccessible;

/*
 * A class the represents the HTML Combobox widget.
 */
class HTMLComboboxAccessible final : public AccessibleWrap {
 public:
  enum { eAction_Click = 0 };

  HTMLComboboxAccessible(nsIContent* aContent, DocAccessible* aDoc);
  virtual ~HTMLComboboxAccessible() = default;

  // LocalAccessible
  virtual EDescriptionValueFlag Description(
      nsString& aDescription) const override;
  virtual void Value(nsString& aValue) const override;
  virtual a11y::role NativeRole() const override;
  virtual uint64_t NativeState() const override;
  virtual bool IsAcceptableChild(nsIContent* aEl) const override;

  // ActionAccessible
  virtual bool HasPrimaryAction() const override;
  virtual void ActionNameAt(uint8_t aIndex, nsAString& aName) override;

  // Widgets
  virtual bool IsWidget() const override;
  virtual bool IsActiveWidget() const override;
  virtual bool AreItemsOperable() const override;
  virtual LocalAccessible* CurrentItem() const override;
  virtual void SetCurrentItem(const LocalAccessible* aItem) override;

  HTMLComboboxListAccessible* List() const;

  /**
   * Return selected option.
   */
  LocalAccessible* SelectedOption() const;
};

/*
 * A class that represents the window that lives to the right
 * of the drop down button inside the Select. This is the window
 * that is made visible when the button is pressed.
 */
class HTMLComboboxListAccessible : public HTMLSelectListAccessible {
 public:
  HTMLComboboxListAccessible(nsIContent* aContent, DocAccessible* aDoc)
      : HTMLSelectListAccessible(aContent, aDoc) {}
  virtual ~HTMLComboboxListAccessible() = default;

  // LocalAccessible
  virtual a11y::role NativeRole() const override;
  virtual uint64_t NativeState() const override;
  virtual nsRect RelativeBounds(nsIFrame** aBoundingFrame) const override;
  virtual bool IsAcceptableChild(nsIContent* aEl) const override;

  // Widgets
  virtual bool IsActiveWidget() const override;
  virtual bool AreItemsOperable() const override;
};

}  // namespace a11y
}  // namespace mozilla

#endif
