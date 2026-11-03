/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMenuItemX.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "nsCocoaUtils.h"
#include "nsMenuBarX.h"
#include "nsMenuItemIconX.h"
#include "nsMenuUtilsX.h"
#include "nsMenuX.h"

#include "nsObjCExceptions.h"

#include "nsCOMPtr.h"
#include "nsGkAtoms.h"

#include "mozilla/ErrorResult.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "nsIWidget.h"

using namespace mozilla;

using mozilla::dom::CallerType;
using mozilla::dom::Event;

// Standard text-editing menu items that route through the macOS responder
// chain via NSResponder-default selectors (cut:, copy:, paste:, undo:, etc.)
// rather than through Gecko's command system. See the action-setup block in
// the constructor.
static bool IsStandardEditMenuItem(nsIContent* aContent) {
  if (!aContent || !aContent->IsElement()) {
    return false;
  }
  dom::Element* element = aContent->AsElement();
  return element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id, u"menu_undo"_ns,
                              eCaseMatters) ||
         element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id, u"menu_redo"_ns,
                              eCaseMatters) ||
         element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id, u"menu_cut"_ns,
                              eCaseMatters) ||
         element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id, u"menu_copy"_ns,
                              eCaseMatters) ||
         element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id,
                              u"menu_paste"_ns, eCaseMatters) ||
         element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id,
                              u"menu_delete"_ns, eCaseMatters) ||
         element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id,
                              u"menu_selectAll"_ns, eCaseMatters);
}

nsMenuItemX::nsMenuItemX(nsMenuX* aParent, const nsString& aLabel,
                         EMenuItemType aItemType,
                         nsMenuGroupOwnerX* aMenuGroupOwner, nsIContent* aNode)
    : mContent(aNode),
      mType(aItemType),
      mMenuParent(aParent),
      mMenuGroupOwner(aMenuGroupOwner) {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  MOZ_COUNT_CTOR(nsMenuItemX);

  MOZ_RELEASE_ASSERT(mContent->IsElement(),
                     "nsMenuItemX should only be created for elements");
  NS_ASSERTION(mMenuGroupOwner, "No menu owner given, must have one!");

  mMenuGroupOwner->RegisterForContentChanges(mContent, this);

  // if we have a command associated with this menu item, register for changes
  // to the command DOM node
  if (dom::Document* doc = mContent->GetUncomposedDoc()) {
    nsAutoString ourCommand;
    mContent->AsElement()->GetAttr(nsGkAtoms::command, ourCommand);

    if (!ourCommand.IsEmpty()) {
      dom::Element* commandElement = doc->GetElementById(ourCommand);

      if (commandElement) {
        mCommandElement = commandElement;
        // register to observe the command DOM element
        mMenuGroupOwner->RegisterForContentChanges(mCommandElement, this);
      }
    }
  }

  if (auto* img = dom::HTMLImageElement::FromNodeOrNull(
          mContent->GetFirstElementChild())) {
    mImageElement = img;
    mMenuGroupOwner->RegisterForContentChanges(mImageElement, this);
  }

  // set up the native menu item
  if (mType == eSeparatorMenuItemType) {
    mNativeMenuItem = [[NSMenuItem separatorItem] retain];
  } else {
    NSString* newCocoaLabelString =
        nsMenuUtilsX::GetTruncatedCocoaLabel(aLabel, mContent);
    mNativeMenuItem = [[GeckoNSMenuItem alloc] initWithTitle:newCocoaLabelString
                                                      action:nil
                                               keyEquivalent:@""];

    SetEnabled();
    SetChecked();
    SetKeyEquiv();
    SetBadge();
    SetAttributedTitle();
    SetIndentationLevel();
    SetTooltip();
  }

  mIcon = std::make_unique<nsMenuItemIconX>(this);

  mIsVisible = !nsMenuUtilsX::NodeIsHiddenOrCollapsed(mContent);

  // Separators don't need actions, targets, or command registration.
  if (mType != eSeparatorMenuItemType) {
    // Most menu items share a single (action, target) pair and are
    // differentiated by a unique (representedObject, tag) pair, but the
    // standard Edit menu items use macOS-default selectors with no fixed
    // target so they propagate via the responder chain. This lets native
    // text fields (e.g. an NSSavePanel sheet's filename field, bug
    // 2036608) handle Cmd+C/V/X/Z/A natively, and ensures that the macOS
    // "Copy" menu item gets the Emoji picker / writing-tools sub-items
    // populated in multi-language environments (bug 1478347). It also
    // lets macOS 26+ auto-inject SF Symbol icons next to the Edit menu
    // items, matching what Safari and other native apps display. When no
    // responder in the chain handles them, our application delegate
    // forwards each selector to
    // [nsMenuBarX::sNativeEventTarget menuItemHit:].
    //
    // undo: and redo: need an extra hop: NSResponder ships with default
    // implementations of those two that look up the responder's
    // undoManager and silently no-op when it is nil, which it always is
    // in Gecko (we use our own TransactionManager). To prevent that
    // default from swallowing the action before the AppDelegate forwarder
    // is reached, ChildView in nsCocoaWindow.mm overrides undo: and redo:
    // to forward to [sNativeEventTarget menuItemHit:] explicitly
    // (bug 2040844).
    SEL standardEditSelector = nil;
    if (mContent->AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id,
                                           u"menu_undo"_ns, eCaseMatters)) {
      standardEditSelector = @selector(undo:);
    } else if (mContent->AsElement()->AttrValueIs(
                   kNameSpaceID_None, nsGkAtoms::id, u"menu_redo"_ns,
                   eCaseMatters)) {
      standardEditSelector = @selector(redo:);
    } else if (mContent->AsElement()->AttrValueIs(kNameSpaceID_None,
                                                  nsGkAtoms::id, u"menu_cut"_ns,
                                                  eCaseMatters)) {
      standardEditSelector = @selector(cut:);
    } else if (mContent->AsElement()->AttrValueIs(
                   kNameSpaceID_None, nsGkAtoms::id, u"menu_copy"_ns,
                   eCaseMatters)) {
      standardEditSelector = @selector(copy:);
    } else if (mContent->AsElement()->AttrValueIs(
                   kNameSpaceID_None, nsGkAtoms::id, u"menu_paste"_ns,
                   eCaseMatters)) {
      standardEditSelector = @selector(paste:);
    } else if (mContent->AsElement()->AttrValueIs(
                   kNameSpaceID_None, nsGkAtoms::id, u"menu_delete"_ns,
                   eCaseMatters)) {
      standardEditSelector = @selector(delete:);
    } else if (mContent->AsElement()->AttrValueIs(
                   kNameSpaceID_None, nsGkAtoms::id, u"menu_selectAll"_ns,
                   eCaseMatters)) {
      standardEditSelector = @selector(selectAll:);
    }

    if (standardEditSelector) {
      mNativeMenuItem.action = standardEditSelector;
    } else {
      mNativeMenuItem.action = @selector(menuItemHit:);
      mNativeMenuItem.target = nsMenuBarX::sNativeEventTarget;
    }

    mNativeMenuItem.representedObject = mMenuGroupOwner->GetRepresentedObject();
    mNativeMenuItem.tag = mMenuGroupOwner->RegisterForCommand(this);
  }

  if (mIsVisible) {
    SetupIcon();
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

nsMenuItemX::~nsMenuItemX() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // autorelease the native menu item so that anything else happening to this
  // object happens before the native menu item actually dies
  [mNativeMenuItem autorelease];

  DetachFromGroupOwner();

  MOZ_COUNT_DTOR(nsMenuItemX);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::DetachFromGroupOwner() {
  if (mMenuGroupOwner) {
    mMenuGroupOwner->UnregisterCommand(mNativeMenuItem.tag);

    if (mContent) {
      mMenuGroupOwner->UnregisterForContentChanges(mContent);
    }
    if (mCommandElement) {
      mMenuGroupOwner->UnregisterForContentChanges(mCommandElement);
    }
    if (mKeyElement) {
      mMenuGroupOwner->UnregisterForContentChanges(mKeyElement);
      mMenuGroupOwner->UnregisterKeyElement(mKeyElement, this);
      mKeyElement = nullptr;
    }
    if (mImageElement) {
      mMenuGroupOwner->UnregisterForContentChanges(mImageElement);
    }
  }

  mMenuGroupOwner = nullptr;
}

nsresult nsMenuItemX::ModifyChecked(bool aIsChecked) {
  // update the content model. This will also handle unchecking our siblings
  // if we are a radiomenu
  mContent->AsElement()->SetBoolAttr(nsGkAtoms::checked, aIsChecked);

  // update native menu item
  SetChecked();

  return NS_OK;
}

EMenuItemType nsMenuItemX::GetMenuItemType() { return mType; }

// Executes the "cached" javaScript command.
// Returns NS_OK if the command was executed properly, otherwise an error code.
void nsMenuItemX::DoCommand(NSEventModifierFlags aModifierFlags,
                            int16_t aButton) {
  // flip "checked" state if we're a checkbox menu, or an un-checked radio menu
  if (mType == eCheckboxMenuItemType ||
      (mType == eRadioMenuItemType && !mIsChecked)) {
    if (!mContent->AsElement()->AttrValueIs(kNameSpaceID_None,
                                            nsGkAtoms::autocheck,
                                            nsGkAtoms::_false, eCaseMatters)) {
      ModifyChecked(!mIsChecked);
    }
    /* the AttributeChanged code will update all the internal state */
  }

  nsMenuUtilsX::DispatchCommandTo(mContent, aModifierFlags, aButton);
}

nsresult nsMenuItemX::DispatchDOMEvent(const nsString& eventName,
                                       bool* preventDefaultCalled) {
  if (!mContent) {
    return NS_ERROR_FAILURE;
  }

  // get owner document for content
  nsCOMPtr<dom::Document> parentDoc = mContent->OwnerDoc();

  // create DOM event
  ErrorResult rv;
  RefPtr<Event> event =
      parentDoc->CreateEvent(u"Events"_ns, CallerType::System, rv);
  if (rv.Failed()) {
    NS_WARNING("Failed to create Event");
    return rv.StealNSResult();
  }
  event->InitEvent(eventName, true, true);

  // mark DOM event as trusted
  event->SetTrusted(true);

  // send DOM event
  *preventDefaultCalled =
      mContent->DispatchEvent(*event, CallerType::System, rv);
  if (rv.Failed()) {
    NS_WARNING("Failed to send DOM event via EventTarget");
    return rv.StealNSResult();
  }

  return NS_OK;
}

// Walk the sibling list looking for nodes with the same name and
// uncheck them all.
void nsMenuItemX::UncheckRadioSiblings(nsIContent* aCheckedContent) {
  nsAutoString myGroupName;
  aCheckedContent->AsElement()->GetAttr(nsGkAtoms::name, myGroupName);
  if (myGroupName.IsEmpty()) {  // no groupname, nothing to do
    return;
  }

  nsCOMPtr<nsIContent> parent = aCheckedContent->GetParent();
  if (!parent) {
    return;
  }

  // loop over siblings
  for (nsIContent* sibling = parent->GetFirstChild(); sibling;
       sibling = sibling->GetNextSibling()) {
    if (sibling != aCheckedContent && sibling->IsElement()) {  // skip this node
      // if the current sibling is in the same group, clear it
      if (sibling->AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name,
                                            myGroupName, eCaseMatters)) {
        sibling->AsElement()->UnsetAttr(kNameSpaceID_None, nsGkAtoms::checked,
                                        true);
      }
    }
  }
}

void nsMenuItemX::SetKeyEquiv() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // Set key shortcut and modifiers
  nsAutoString keyValue;
  mContent->AsElement()->GetAttr(nsGkAtoms::key, keyValue);

  RefPtr<dom::Element> keyContent;
  if (!keyValue.IsEmpty() && mContent->GetUncomposedDoc()) {
    keyContent = mContent->GetUncomposedDoc()->GetElementById(keyValue);
  }

  // The element we resolve to changes when our key attribute does.
  if (keyContent != mKeyElement) {
    if (mMenuGroupOwner) {
      if (mKeyElement) {
        mMenuGroupOwner->UnregisterForContentChanges(mKeyElement);
        mMenuGroupOwner->UnregisterKeyElement(mKeyElement, this);
      }
      if (keyContent) {
        mMenuGroupOwner->RegisterForContentChanges(keyContent, this);
        mMenuGroupOwner->RegisterKeyElement(keyContent, this);
      }
    }
    mKeyElement = keyContent;
  }

  NSString* keyEquivalent = @"";
  NSEventModifierFlags macModifiers = 0;
  if (keyContent) {
    // The native item carries the built-in shortcut. A shortcut the user
    // assigned in System Settings is applied on top of it by AppKit, and
    // taken away again by AppKit when the user removes it.
    nsMenuGroupOwnerX::KeyElementAttributes attributes =
        nsMenuGroupOwnerX::BuiltInKeyElementAttributes(keyContent);

    nsAutoString keyChar;
    if (attributes.mKey && !attributes.mKey->IsEmpty()) {
      keyChar = *attributes.mKey;
    } else {
      uint32_t charCode = nsCocoaUtils::ConvertGeckoNameToMacCharCode(
          attributes.mKeyCode.valueOr(EmptyString()));
      if (charCode) {
        keyChar.Assign(charCode);
      } else {
        keyChar.AssignLiteral(u" ");
      }
    }

    uint8_t modifiers = nsMenuUtilsX::GeckoModifiersForNodeAttribute(
        attributes.mModifiers.valueOr(EmptyString()));
    macModifiers = nsMenuUtilsX::MacModifiersForGeckoModifiers(modifiers);

    keyEquivalent =
        [[NSString stringWithCharacters:(unichar*)keyChar.get()
                                 length:keyChar.Length()] lowercaseString];
    if ([keyEquivalent isEqualToString:@" "]) {
      keyEquivalent = @"";
    }
  }

  // Setting these posts NSMenuDidChangeItemNotification, so only touch what
  // changed.
  if (mNativeMenuItem.keyEquivalentModifierMask != macModifiers) {
    mNativeMenuItem.keyEquivalentModifierMask = macModifiers;
  }
  if (![mNativeMenuItem.keyEquivalent isEqualToString:keyEquivalent]) {
    mNativeMenuItem.keyEquivalent = keyEquivalent;
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::SetBadge() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (@available(macOS 14.0, *)) {
    nsAutoString badgeValue;
    if (!mContent->AsElement()->GetAttr(nsGkAtoms::badge, badgeValue)) {
      mNativeMenuItem.badge = nullptr;
      return;
    }
    mNativeMenuItem.badge = [[[NSMenuItemBadge alloc]
        initWithString:nsCocoaUtils::ToNSString(badgeValue)] autorelease];
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::SetTitle() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  nsAutoString newLabel;
  mContent->AsElement()->GetAttr(nsGkAtoms::label, newLabel);
  mNativeMenuItem.title =
      nsMenuUtilsX::GetTruncatedCocoaLabel(newLabel, mContent);

  SetAttributedTitle();
  // System Settings assigns shortcuts by title.
  CopyUserKeyEquivalentToKeyElement();

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::CopyUserKeyEquivalentToKeyElement() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (mType == eSeparatorMenuItemType) {
    return;
  }
  if (!mNativeMenuItem.menu) {
    // A visible item gets here again once it is inserted, but a hidden item
    // is not in its menu. AppKit applies the assigned shortcut when the item
    // is added to a menu, and the item keeps it once removed, so add a hidden
    // item to its menu for a moment. It has to be its own menu, as System
    // Settings can name the item by its menu path.
    NSMenu* parentMenu = mMenuParent ? mMenuParent->NativeNSMenu() : nil;
    if (!mKeyElement || !parentMenu ||
        !nsMenuUtilsX::NodeIsHiddenOrCollapsed(mContent)) {
      return;
    }
    mNativeMenuItem.hidden = YES;
    [parentMenu addItem:mNativeMenuItem];
    [parentMenu removeItem:mNativeMenuItem];
    mNativeMenuItem.hidden = NO;
  }
  nsMenuGroupOwnerX::CopyUserKeyEquivalentToKeyElement(mNativeMenuItem,
                                                       mContent->AsElement());

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::SetAttributedTitle() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mMenuParent->IsAnchoredPopUp()) {
    // Use attributed titles only on anchored popups so that pulldowns and
    // context menus always use native default sizing.
    return;
  }

  if (NSAttributedString* attrString = nsMenuUtilsX::AttributedStringForContent(
          mContent, mNativeMenuItem.title)) {
    mNativeMenuItem.attributedTitle = attrString;
  } else {
    mNativeMenuItem.attributedTitle = nil;
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::SetChecked() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  mIsChecked = mContent->AsElement()->GetBoolAttr(nsGkAtoms::checked) ||
               mContent->AsElement()->GetBoolAttr(nsGkAtoms::selected);

  mNativeMenuItem.state =
      mIsChecked ? NSControlStateValueOn : NSControlStateValueOff;

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::SetEnabled() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  bool isEnabled;
  if (IsStandardEditMenuItem(mContent)) {
    // Standard Edit menu items dispatch via the macOS responder chain, so let
    // AppKit's automatic validateUserInterfaceItem: walk decide the real
    // enable/disable state. Mirroring the XUL command's `disabled` attribute
    // here would suppress that walk -- AppKit's performKeyEquivalent: also
    // *consumes* the keystroke for a disabled matching menu item without
    // firing the action, so a stale XUL-disabled state would silently kill
    // shortcuts like Cmd+Shift+Z inside a native sheet, not just grey the
    // menu out (bug 2040851).
    isEnabled = true;
  } else if (mCommandElement) {
    isEnabled = !mCommandElement->GetBoolAttr(nsGkAtoms::disabled);
  } else if (mContent->IsXULElement(nsGkAtoms::menucaption)) {
    isEnabled = false;
  } else {
    isEnabled = !mContent->AsElement()->GetBoolAttr(nsGkAtoms::disabled);
  }

  mNativeMenuItem.enabled = isEnabled;

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::SetIndentationLevel() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (mContent->AsElement()->GetBoolAttr(nsGkAtoms::indented)) {
    mNativeMenuItem.indentationLevel = 1;
  } else {
    mNativeMenuItem.indentationLevel = 0;
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::SetTooltip() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  nsAutoString tooltip;
  if (!mContent->AsElement()->GetAttr(nsGkAtoms::tooltiptext, tooltip)) {
    mNativeMenuItem.toolTip = nil;
    return;
  }

  mNativeMenuItem.toolTip = nsCocoaUtils::ToNSString(tooltip);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuItemX::Dump(uint32_t aIndent) const {
  printf("%*s - item [%p] %-16s <%s>\n", aIndent * 2, "", this,
         mType == eSeparatorMenuItemType ? "----"
                                         : [mNativeMenuItem.title UTF8String],
         NS_ConvertUTF16toUTF8(mContent->NodeName()).get());
}

//
// nsChangeObserver
//

void nsMenuItemX::ObserveAttributeChanged(dom::Document* aDocument,
                                          nsIContent* aContent,
                                          nsAtom* aAttribute) {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!aContent) {
    return;
  }

  if (aContent == mContent) {  // our own content node changed
    if (aAttribute == nsGkAtoms::checked || aAttribute == nsGkAtoms::selected) {
      // if we're a radio menu, uncheck our sibling radio items. No need to
      // do any of this if we're just a normal check menu.
      // XXX isn't this done by XULButtonElement as well?
      if (aAttribute == nsGkAtoms::checked && mType == eRadioMenuItemType &&
          mContent->AsElement()->GetBoolAttr(nsGkAtoms::checked)) {
        UncheckRadioSiblings(mContent);
      }
      SetChecked();
      mMenuParent->SetRebuild(true);
    } else if (aAttribute == nsGkAtoms::hidden ||
               aAttribute == nsGkAtoms::collapsed) {
      bool isVisible = !nsMenuUtilsX::NodeIsHiddenOrCollapsed(mContent);
      if (isVisible != mIsVisible) {
        mIsVisible = isVisible;
        RefPtr<nsMenuItemX> self = this;
        mMenuParent->MenuChildChangedVisibility(nsMenuParentX::MenuChild(self),
                                                isVisible);
        if (mIsVisible) {
          SetupIcon();
        }
      }
      mMenuParent->SetRebuild(true);
    } else if (aAttribute == nsGkAtoms::label) {
      if (mType != eSeparatorMenuItemType) {
        SetTitle();
      }
    } else if (aAttribute == nsGkAtoms::badge) {
      SetBadge();
    } else if (aAttribute == nsGkAtoms::key) {
      SetKeyEquiv();
      CopyUserKeyEquivalentToKeyElement();
    } else if (aAttribute == nsGkAtoms::disabled) {
      SetEnabled();
    } else if (aAttribute == nsGkAtoms::indented) {
      SetIndentationLevel();
    } else if (aAttribute == nsGkAtoms::tooltiptext) {
      SetTooltip();
    }
  } else if (aContent == mCommandElement) {
    // the only thing that really matters when the menu isn't showing is the
    // enabled state since it enables/disables keyboard commands
    if (aAttribute == nsGkAtoms::disabled) {
      // first we sync our menu item DOM node with the command DOM node
      const bool commandDisabled =
          mCommandElement->GetBoolAttr(nsGkAtoms::disabled);
      const bool menuDisabled =
          mContent->AsElement()->GetBoolAttr(nsGkAtoms::disabled);
      if (commandDisabled != menuDisabled) {
        mContent->AsElement()->SetBoolAttr(nsGkAtoms::disabled,
                                           commandDisabled);
      }
      // now we sync our native menu item with the command DOM node
      SetEnabled();
    }
  } else if (aContent == mKeyElement) {
    if (aAttribute == nsGkAtoms::key || aAttribute == nsGkAtoms::keycode ||
        aAttribute == nsGkAtoms::modifiers ||
        aAttribute == nsGkAtoms::databuiltinkey ||
        aAttribute == nsGkAtoms::databuiltinkeycode ||
        aAttribute == nsGkAtoms::databuiltinmodifiers ||
        aAttribute == nsGkAtoms::datahassystemshortcut) {
      SetKeyEquiv();
    }
  } else if (aContent == mImageElement && aAttribute == nsGkAtoms::srcset) {
    SetupIcon();
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

bool IsMenuStructureElement(nsIContent* aContent) {
  return aContent->IsAnyOfXULElements(nsGkAtoms::menu, nsGkAtoms::menucaption,
                                      nsGkAtoms::menuitem,
                                      nsGkAtoms::menuseparator);
}

void nsMenuItemX::ObserveContentRemoved(dom::Document* aDocument,
                                        nsIContent* aContainer,
                                        nsIContent* aChild) {
  MOZ_RELEASE_ASSERT(mMenuGroupOwner);
  MOZ_RELEASE_ASSERT(mMenuParent);

  if (aChild == mCommandElement) {
    mMenuGroupOwner->UnregisterForContentChanges(mCommandElement);
    mCommandElement = nullptr;
  }
  if (aChild == mImageElement) {
    mMenuGroupOwner->UnregisterForContentChanges(mImageElement);
    mImageElement = nullptr;
    SetupIcon();
  }
  if (IsMenuStructureElement(aChild)) {
    mMenuParent->SetRebuild(true);
  }
}

void nsMenuItemX::ObserveContentInserted(dom::Document* aDocument,
                                         nsIContent* aContainer,
                                         nsIContent* aChild) {
  MOZ_RELEASE_ASSERT(mMenuGroupOwner);
  MOZ_RELEASE_ASSERT(mMenuParent);

  // The child node could come from the custom element that is for display, so
  // only rebuild the menu if the child is related to the structure of the
  // menu.
  if (IsMenuStructureElement(aChild)) {
    mMenuParent->SetRebuild(true);
  }

  if (!mImageElement && aContainer == mContent &&
      aChild->IsHTMLElement(nsGkAtoms::img)) {
    mImageElement = aChild->AsElement();
    mMenuGroupOwner->RegisterForContentChanges(aChild, this);
    SetupIcon();
  }
}

void nsMenuItemX::SetupIcon() {
  if (mType != eRegularMenuItemType) {
    // Don't support icons on checkbox and radio menuitems, for consistency with
    // Windows & Linux.
    return;
  }

  mIcon->SetupIcon(mContent);
  mNativeMenuItem.image = mIcon->GetIconImage();
}

void nsMenuItemX::IconUpdated() {
  mNativeMenuItem.image = mIcon->GetIconImage();
}
