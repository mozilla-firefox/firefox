/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMenuItemX_h_
#define nsMenuItemX_h_

#import <Cocoa/Cocoa.h>

#include "mozilla/RefPtr.h"
#include "nsChangeObserver.h"
#include "nsISupports.h"
#include "nsMenuGroupOwnerX.h"
#include "nsMenuItemIconX.h"
#include "nsStringFwd.h"

class nsMenuItemIconX;
class nsMenuX;
class nsMenuParentX;

namespace mozilla {
namespace dom {
class Element;
}
}  // namespace mozilla

enum {
  knsMenuItemNoModifier = 0,
  knsMenuItemShiftModifier = (1 << 0),
  knsMenuItemAltModifier = (1 << 1),
  knsMenuItemControlModifier = (1 << 2),
  knsMenuItemCommandModifier = (1 << 3)
};

enum EMenuItemType {
  eRegularMenuItemType = 0,
  eCheckboxMenuItemType,
  eRadioMenuItemType,
  eSeparatorMenuItemType
};

// Once instantiated, this object lives until its DOM node or its parent window
// is destroyed. Do not hold references to this, they can become invalid any
// time the DOM node can be destroyed.
class nsMenuItemX final : public nsChangeObserver,
                          public nsMenuItemIconX::Listener {
 public:
  nsMenuItemX(nsMenuX* aParent, const nsString& aLabel, EMenuItemType aItemType,
              nsMenuGroupOwnerX* aMenuGroupOwner, nsIContent* aNode);

  bool IsVisible() const { return mIsVisible; }

  // Unregisters nsMenuX from the nsMenuGroupOwner, and nulls out the group
  // owner pointer. This is needed because nsMenuX is reference-counted and can
  // outlive its owner, and the menu group owner asserts that everything has
  // been unregistered when it is destroyed.
  void DetachFromGroupOwner();

  // Nulls out our reference to the parent.
  // This is needed because nsMenuX is reference-counted and can outlive its
  // parent.
  void DetachFromParent() { mMenuParent = nullptr; }

  NS_INLINE_DECL_REFCOUNTING(nsMenuItemX)

  NS_DECL_CHANGEOBSERVER

  // nsMenuItemIconX::Listener
  void IconUpdated() override;

  // nsMenuItemX
  nsresult ModifyChecked(bool aIsChecked);
  EMenuItemType GetMenuItemType();
  void DoCommand(NSEventModifierFlags aModifierFlags, int16_t aButton);
  nsresult DispatchDOMEvent(const nsString& eventName,
                            bool* preventDefaultCalled);
  void SetupIcon();
  bool HasDeferredIconLoad() const { return mIcon->HasDeferredLoad(); }
  nsMenuX* ParentMenu() { return mMenuParent; }
  nsIContent* Content() { return mContent; }
  NSMenuItem* NativeNSMenuItem() { return mNativeMenuItem; }

  // Mirrors a shortcut the user assigned to this item in System Settings into
  // the XUL key element, or takes it back out. AppKit applies such shortcuts
  // once the native item is in a menu, so this runs after insertion.
  void CopyUserKeyEquivalentToKeyElement();

  // The shortcut the item has from its key element, underneath a shortcut the
  // user assigned in System Settings.
  NSString* BuiltInKeyEquivalent() { return mBuiltInKeyEquivalent; }
  NSEventModifierFlags BuiltInKeyEquivalentModifierMask() {
    return mBuiltInKeyEquivalentModifierMask;
  }

  // Whether the item's shortcut is internal, so that its key element never
  // runs the command itself. As in XULKeySetGlobalKeyListener, this is the
  // internal attribute of the command element the key element points to, or
  // of the key element when it has no command.
  bool HasInternalShortcut();

  void Dump(uint32_t aIndent) const;

 protected:
  virtual ~nsMenuItemX();

  void UncheckRadioSiblings(nsIContent* aCheckedElement);
  void SetKeyEquiv();
  void SetBadge();
  void SetTitle();
  void SetAttributedTitle();
  void SetChecked();
  void SetEnabled();
  void SetIndentationLevel();
  void SetTooltip();

  // XUL <menucaption>, <menuitem>, or <menuseparator>
  nsCOMPtr<nsIContent> mContent;

  EMenuItemType mType;

  // nsMenuItemX objects should always have a valid native menu item.
  NSMenuItem* mNativeMenuItem = nil;             // [strong]
  nsMenuX* mMenuParent = nullptr;                // [weak]
  nsMenuGroupOwnerX* mMenuGroupOwner = nullptr;  // [weak]
  RefPtr<mozilla::dom::Element> mCommandElement;
  // The <key> element mContent's key attribute resolves to, observed so that
  // edits to it reach the native menu item.
  RefPtr<mozilla::dom::Element> mKeyElement;
  NSString* mBuiltInKeyEquivalent = @"";  // [strong]
  NSEventModifierFlags mBuiltInKeyEquivalentModifierMask = 0;
  RefPtr<mozilla::dom::Element> mImageElement;
  mozilla::UniquePtr<nsMenuItemIconX> mIcon;  // always non-null
  bool mIsChecked = false;
  bool mIsVisible = false;
};

#endif  // nsMenuItemX_h_
