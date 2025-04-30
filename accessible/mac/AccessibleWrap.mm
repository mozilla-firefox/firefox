/* clang-format off */
/* -*- Mode: Objective-C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* clang-format on */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DocAccessibleWrap.h"
#include "nsObjCExceptions.h"
#include "nsCocoaUtils.h"
#include "nsUnicharUtils.h"

#include "LocalAccessible-inl.h"
#include "nsAccUtils.h"
#include "mozilla/a11y/Role.h"
#include "TextRange.h"
#include "gfxPlatform.h"

#import "MOXLandmarkAccessibles.h"
#import "MOXMathAccessibles.h"
#import "MOXOuterDoc.h"
#import "MOXTextMarkerDelegate.h"
#import "MOXWebAreaAccessible.h"
#import "mozAccessible.h"
#import "mozActionElements.h"
#import "mozHTMLAccessible.h"
#import "mozSelectableElements.h"
#import "mozTableAccessible.h"
#import "mozTextAccessible.h"

using namespace mozilla;
using namespace mozilla::a11y;

AccessibleWrap::AccessibleWrap(nsIContent* aContent, DocAccessible* aDoc)
    : LocalAccessible(aContent, aDoc),
      mNativeObject(nil),
      mNativeInited(false) {
  if (aContent && aDoc && IsLiveRegion(aContent)) {
    // Check if this accessible is a live region and queue it
    // it for dispatching an event after it has been inserted.
    DocAccessibleWrap* doc = static_cast<DocAccessibleWrap*>(aDoc);
    doc->QueueNewLiveRegion(this);
  }
}

AccessibleWrap::~AccessibleWrap() {}

mozAccessible* AccessibleWrap::GetNativeObject() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!mNativeInited && !mNativeObject) {
    // We don't creat OSX accessibles for xul tooltips, defunct accessibles,
    // <br> (whitespace) elements, or pruned children.
    //
    // To maintain a scripting environment where the XPCOM accessible hierarchy
    // look the same on all platforms, we still let the C++ objects be created
    // though.
    if (!IsXULTooltip() && !IsDefunct() && Role() != roles::WHITESPACE) {
      mNativeObject = [[GetNativeType() alloc] initWithAccessible:this];
    }
  }

  mNativeInited = true;

  return mNativeObject;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

void AccessibleWrap::GetNativeInterface(void** aOutInterface) {
  *aOutInterface = static_cast<void*>(GetNativeObject());
}

// overridden in subclasses to create the right kind of object. by default we
// create a generic 'mozAccessible' node.
Class AccessibleWrap::GetNativeType() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (IsXULTabpanels()) {
    return [mozPaneAccessible class];
  }

  if (IsTable()) {
    return [mozTableAccessible class];
  }

  if (IsTableRow()) {
    return [mozTableRowAccessible class];
  }

  if (IsTableCell()) {
    return [mozTableCellAccessible class];
  }

  if (IsDoc()) {
    return [MOXWebAreaAccessible class];
  }

  if (IsOuterDoc()) {
    return [MOXOuterDoc class];
  }

  if (IsTextField() && !HasNumericValue()) {
    return [mozTextAccessible class];
  }

  return GetTypeFromRole(Role());

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

// this method is very important. it is fired when an accessible object "dies".
// after this point the object might still be around (because some 3rd party
// still has a ref to it), but it is in fact 'dead'.
void AccessibleWrap::Shutdown() {
  // this ensure we will not try to re-create the native object.
  mNativeInited = true;

  // we really intend to access the member directly.
  if (mNativeObject) {
    [mNativeObject expire];
    [mNativeObject release];
    mNativeObject = nil;
  }

  LocalAccessible::Shutdown();
}

nsresult AccessibleWrap::HandleAccEvent(AccEvent* aEvent) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  nsresult rv = LocalAccessible::HandleAccEvent(aEvent);
  NS_ENSURE_SUCCESS(rv, rv);

  if (IsDefunct()) {
    // The accessible can become defunct after their events are handled.
    return NS_OK;
  }

  uint32_t eventType = aEvent->GetEventType();

  if (eventType == nsIAccessibleEvent::EVENT_SHOW) {
    DocAccessibleWrap* doc = static_cast<DocAccessibleWrap*>(Document());
    doc->ProcessNewLiveRegions();
  }

  if ((eventType == nsIAccessibleEvent::EVENT_TEXT_INSERTED ||
       eventType == nsIAccessibleEvent::EVENT_TEXT_REMOVED ||
       eventType == nsIAccessibleEvent::EVENT_NAME_CHANGE) &&
      !aEvent->FromUserInput()) {
    for (LocalAccessible* container = aEvent->GetAccessible(); container;
         container = container->LocalParent()) {
      if (container->HasOwnContent() && IsLiveRegion(container->GetContent())) {
        // We rely on EventQueue::CoalesceEvents to remove duplicates
        Document()->FireDelayedEvent(
            nsIAccessibleEvent::EVENT_LIVE_REGION_CHANGED, container);
      }
    }
  }

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

bool AccessibleWrap::IsLiveRegion(nsIContent* aContent) {
  if (!aContent || !aContent->IsElement()) {
    return false;
  }

  static const dom::Element::AttrValuesArray sLiveRegionValues[] = {
      nsGkAtoms::OFF, nsGkAtoms::polite, nsGkAtoms::assertive, nullptr};
  int32_t attrValue = nsAccUtils::FindARIAAttrValueIn(
      aContent->AsElement(), nsGkAtoms::aria_live, sLiveRegionValues,
      eIgnoreCase);
  if (attrValue == 0) {
    // aria-live is "off", do nothing.
  } else if (attrValue > 0) {
    // aria-live attribute is polite or assertive. It's live!
    return true;
  } else if (const nsRoleMapEntry* roleMap =
                 aria::GetRoleMap(aContent->AsElement())) {
    // aria role defines it as a live region. It's live!
    if (roleMap->liveAttRule == ePoliteLiveAttr ||
        roleMap->liveAttRule == eAssertiveLiveAttr) {
      return true;
    }
  } else if (nsStaticAtom* value = GetAccService()->MarkupAttribute(
                 aContent, nsGkAtoms::aria_live)) {
    // HTML element defines it as a live region. It's live!
    if (value == nsGkAtoms::polite || value == nsGkAtoms::assertive) {
      return true;
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
// AccessibleWrap protected

Class a11y::GetTypeFromRole(roles::Role aRole) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  switch (aRole) {
    case roles::COMBOBOX:
      return [mozPopupButtonAccessible class];

    case roles::PUSHBUTTON:
      return [mozButtonAccessible class];

    case roles::PAGETAB:
      return [mozTabAccessible class];

    case roles::DATE_EDITOR:
      return [mozDatePickerAccessible class];

    case roles::CHECKBUTTON:
    case roles::TOGGLE_BUTTON:
    case roles::SWITCH:
    case roles::CHECK_MENU_ITEM:
      return [mozCheckboxAccessible class];

    case roles::RADIOBUTTON:
    case roles::RADIO_MENU_ITEM:
      return [mozRadioButtonAccessible class];

    case roles::PROGRESSBAR:
      return [mozRangeAccessible class];

    case roles::METER:
      return [mozMeterAccessible class];

    case roles::SPINBUTTON:
    case roles::SLIDER:
      return [mozIncrementableAccessible class];

    case roles::HEADING:
      return [mozHeadingAccessible class];

    case roles::PAGETABLIST:
      return [mozTabGroupAccessible class];

    case roles::ENTRY:
    case roles::PASSWORD_TEXT:
      return [mozTextAccessible class];

    case roles::TEXT_LEAF:
    case roles::STATICTEXT:
      return [mozTextLeafAccessible class];

    case roles::LANDMARK:
      return [MOXLandmarkAccessible class];

    case roles::LINK:
      return [mozLinkAccessible class];

    case roles::LISTBOX:
      return [mozListboxAccessible class];

    case roles::LISTITEM:
      return [MOXListItemAccessible class];

    case roles::OPTION: {
      return [mozOptionAccessible class];
    }

    case roles::RICH_OPTION: {
      return [mozSelectableChildAccessible class];
    }

    case roles::COMBOBOX_LIST:
    case roles::MENUBAR:
    case roles::MENUPOPUP: {
      return [mozMenuAccessible class];
    }

    case roles::COMBOBOX_OPTION:
    case roles::PARENT_MENUITEM:
    case roles::MENUITEM: {
      return [mozMenuItemAccessible class];
    }

    case roles::MATHML_ROOT:
      return [MOXMathRootAccessible class];

    case roles::MATHML_SQUARE_ROOT:
      return [MOXMathSquareRootAccessible class];

    case roles::MATHML_FRACTION:
      return [MOXMathFractionAccessible class];

    case roles::MATHML_SUB:
    case roles::MATHML_SUP:
    case roles::MATHML_SUB_SUP:
      return [MOXMathSubSupAccessible class];

    case roles::MATHML_UNDER:
    case roles::MATHML_OVER:
    case roles::MATHML_UNDER_OVER:
      return [MOXMathUnderOverAccessible class];

    case roles::OUTLINE:
    case roles::TREE_TABLE:
      return [mozOutlineAccessible class];

    case roles::OUTLINEITEM:
      return [mozOutlineRowAccessible class];

    case roles::LABEL:
      return [MOXLabelAccessible class];

    default:
      return [mozAccessible class];
  }

  return nil;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}
