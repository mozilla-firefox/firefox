/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMenuUtilsX_h_
#define nsMenuUtilsX_h_

#include "mozilla/AlreadyAddRefed.h"
#include "nsStringFwd.h"
#include "nscore.h"

#import <Cocoa/Cocoa.h>

class nsIContent;
class nsMenuBarX;
class nsMenuX;

// Namespace containing utility functions used in our native menu
// implementation.
namespace nsMenuUtilsX {
void DispatchCommandTo(nsIContent* aTargetContent,
                       NSEventModifierFlags aModifierFlags, int16_t aButton);
// Returns aLabel cut at the end with an ellipsis so that it fits within the
// computed max-width of aContent, the same limit that crop="end" applies to
// non-native menus.
NSString* GetTruncatedCocoaLabel(const nsString& aLabel, nsIContent* aContent);
// Returns aLabel unchanged if it fits within aMaxWidth when drawn in aFont,
// otherwise the longest prefix that fits with an ellipsis appended.
NSString* TruncateLabelToWidth(NSString* aLabel, NSFont* aFont,
                               CGFloat aMaxWidth);
uint8_t GeckoModifiersForNodeAttribute(const nsString& modifiersAttribute);
unsigned int MacModifiersForGeckoModifiers(uint8_t geckoModifiers);
already_AddRefed<nsMenuBarX> GetHiddenWindowMenuBar();
bool NodeIsHiddenOrCollapsed(nsIContent* aContent);
NSAttributedString* AttributedStringForContent(nsIContent* aContent,
                                               NSString* aLabel);

// Find the menu item by following the path aLocationString from aRootMenu.
// aLocationString is a '|'-separated list of integers, where each integer is
// the index of the menu item in the menu.
// aIsMenuBar needs to be true if aRootMenu is the app's mainMenu, so that the
// app menu can be skipped during the search.
NSMenuItem* NativeMenuItemWithLocation(NSMenu* aRootMenu,
                                       NSString* aLocationString,
                                       bool aIsMenuBar);

// Traverse the menu tree and check that there are no cycles or NSMenu(Item)
// objects that are used more than once. If inconsistencies are found, these
// functions crash the process.
void CheckNativeMenuConsistency(NSMenu* aMenu);
void CheckNativeMenuConsistency(NSMenuItem* aMenuItem);

// Print out debugging information about the native menu tree structure.
void DumpNativeMenu(NSMenu* aMenu);
void DumpNativeMenuItem(NSMenuItem* aMenuItem);

extern bool gIsSynchronouslyActivatingNativeMenuItemDuringTest;

}  // namespace nsMenuUtilsX

#endif  // nsMenuUtilsX_h_
