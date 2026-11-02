/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMenuX.h"

#include <_types/_uint32_t.h>
#include <dlfcn.h>

#include "mozilla/EventDispatcher.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ScriptSettings.h"

#include "MOZMenuOpeningCoordinator.h"
#include "nsMenuItemIconX.h"
#include "nsMenuItemX.h"
#include "nsMenuUtilsX.h"

#include "nsObjCExceptions.h"

#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsCocoaUtils.h"
#include "nsComputedDOMStyle.h"
#include "nsGkAtoms.h"
#include "nsIWidget.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "nsToolkit.h"
#include "nsUnicharUtils.h"
#include "prinrval.h"

#include "nsIComponentManager.h"
#include "nsIContent.h"
#include "nsIDocumentObserver.h"
#include "nsIRollupListener.h"
#include "nsIServiceManager.h"
#include "nsXULPopupManager.h"

using namespace mozilla;
using namespace mozilla::dom;

static RefPtr<nsIContent> GetMenuChildContent(
    const nsMenuParentX::MenuChild& aChild) {
  return aChild.match(
      [](const RefPtr<nsMenuX>& aMenu) { return aMenu->Content(); },
      [](const RefPtr<nsMenuItemX>& aMenuItem) {
        return aMenuItem->Content();
      });
}

// Global rather than per-instance: building a menu triggers DOM observer
// callbacks on child/sibling nsMenuX instances, and we need to suppress those
// across all instances to prevent re-entrant rebuilds.
static bool gConstructingMenu = false;

static bool gMenuMethodsSwizzled = false;

// Protect against really deep menu nestings, for example from recursive
// bookmark folders. This avoids hangs when the system enumerates the entire
// menu tree.
static const size_t kMaxMenuNestingDepth = 20;

int32_t nsMenuX::sIndexingMenuLevel = 0;

// TODO: It is unclear whether this is still needed.
static void SwizzleDynamicIndexingMethods() {
  if (gMenuMethodsSwizzled) {
    return;
  }

  nsToolkit::SwizzleMethods([NSMenu class], @selector(_addItem:toTable:),
                            @selector(nsMenuX_NSMenu_addItem:toTable:), true);
  nsToolkit::SwizzleMethods([NSMenu class], @selector(_removeItem:fromTable:),
                            @selector(nsMenuX_NSMenu_removeItem:fromTable:),
                            true);
  // On SnowLeopard the Shortcut framework (which contains the
  // SCTGRLIndex class) is loaded on demand, whenever the user first opens
  // a menu (which normally hasn't happened yet).  So we need to load it
  // here explicitly.
  dlopen("/System/Library/PrivateFrameworks/Shortcut.framework/Shortcut",
         RTLD_LAZY);
  Class SCTGRLIndexClass = ::NSClassFromString(@"SCTGRLIndex");
  nsToolkit::SwizzleMethods(
      SCTGRLIndexClass, @selector(indexMenuBarDynamically),
      @selector(nsMenuX_SCTGRLIndex_indexMenuBarDynamically));

  Class NSServicesMenuUpdaterClass =
      ::NSClassFromString(@"_NSServicesMenuUpdater");
  nsToolkit::SwizzleMethods(
      NSServicesMenuUpdaterClass,
      @selector(populateMenu:withServiceEntries:forDisplay:),
      @selector(nsMenuX_populateMenu:withServiceEntries:forDisplay:));

  gMenuMethodsSwizzled = true;
}

//
// nsMenuX
//

nsMenuX::nsMenuX(nsMenuParentX* aParent, nsMenuGroupOwnerX* aMenuGroupOwner,
                 nsIContent* aContent)
    : mContent(aContent),
      mParent(aParent),
      mMenuGroupOwner(aMenuGroupOwner),
      mNestingDepth(aParent ? aParent->NestingDepth() + 1 : 0) {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  MOZ_COUNT_CTOR(nsMenuX);

  SwizzleDynamicIndexingMethods();

  mMenuDelegate = [[MenuDelegate alloc] initWithGeckoMenu:this];

  if (!nsMenuBarX::sNativeEventTarget) {
    nsMenuBarX::sNativeEventTarget = [[NativeMenuItemTarget alloc] init];
  }

  bool shouldShowServices = false;
  if (mContent->IsElement()) {
    mContent->AsElement()->GetAttr(nsGkAtoms::label, mLabel);

    shouldShowServices =
        mContent->AsElement()->HasAttr(nsGkAtoms::showservicesmenu);
  }
  mNativeMenu = CreateMenuWithGeckoString(mLabel, shouldShowServices);

  // register this menu to be notified when changes are made to our content
  // object
  NS_ASSERTION(mMenuGroupOwner, "No menu owner given, must have one");
  mMenuGroupOwner->RegisterForContentChanges(mContent, this);

  mVisible = !nsMenuUtilsX::NodeIsHiddenOrCollapsed(mContent);

  NSString* newCocoaLabelString =
      nsMenuUtilsX::GetTruncatedCocoaLabel(mLabel, mContent);
  mNativeMenuItem = [[GeckoNSMenuItem alloc] initWithTitle:newCocoaLabelString
                                                    action:nil
                                             keyEquivalent:@""];
  mNativeMenuItem.submenu = mNativeMenu;

  if (mNestingDepth > kMaxMenuNestingDepth) {
    // If we're nested too deep, turn this item into a regular item without a
    // submenu.
    mNativeMenuItem.submenu = nil;
  }

  SetAttributedTitle();
  SetEnabled(!mContent->IsElement() ||
             !mContent->AsElement()->GetBoolAttr(nsGkAtoms::disabled));

  // We call RebuildMenu here because keyboard commands are dependent upon
  // native menu items being created. If we only call RebuildMenu when a menu
  // is actually selected, then we can't access keyboard commands until the
  // menu gets selected, which is bad.
  RebuildMenu();

  bool isXULWindowMenu = IsXULWindowMenu(mContent);
  if (isXULWindowMenu) {
    // Let the OS know that this is our Window menu.
    NSApp.windowsMenu = mNativeMenu;
  }

  mIcon = std::make_unique<nsMenuItemIconX>(this);

  if (mVisible) {
    if (!isXULWindowMenu && !IsXULEditMenu(mContent)) {
      SetRebuild(true);
    }
    SetupIcon();
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

nsMenuX::~nsMenuX() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // This needs to be the first thing done here so that any DOM events sent
  // later in this destructor can't refer back to this object.
  DetachFromGroupOwnerRecursive();

  // Make sure a pending popupshown event isn't dropped.
  FlushMenuOpenedRunnable();

  if (mIsOpen) {
    [mNativeMenu cancelTracking];
    MOZMenuOpeningCoordinator.needToUnwindForMenuClosing = YES;
  }

  // Make sure pending popuphiding/popuphidden events aren't dropped.
  FlushMenuClosedRunnable();

  OnHighlightedItemChanged(Nothing());
  RemoveAll();

  mNativeMenu.delegate = nil;
  [mNativeMenu release];
  [mMenuDelegate release];
  // autorelease the native menu item so that anything else happening to this
  // object happens before the native menu item actually dies
  [mNativeMenuItem autorelease];

  MOZ_COUNT_DTOR(nsMenuX);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuX::DetachFromGroupOwnerRecursive() {
  if (!mMenuGroupOwner) {
    // Don't recurse if this subtree is already detached.
    // This avoids repeated recursion during the destruction of nested nsMenuX
    // structures. Our invariant is: If we are detached, all of our contents are
    // also detached.
    return;
  }

  if (mContent) {
    mMenuGroupOwner->UnregisterForContentChanges(mContent);
  }
  mMenuGroupOwner = nullptr;

  // Also detach all our children.
  for (auto& child : mMenuChildren) {
    child.match(
        [](const RefPtr<nsMenuX>& aMenu) {
          aMenu->DetachFromGroupOwnerRecursive();
        },
        [](const RefPtr<nsMenuItemX>& aMenuItem) {
          aMenuItem->DetachFromGroupOwner();
        });
  }
}

void nsMenuX::OnMenuWillOpen(dom::Element* aPopupElement) {
  RefPtr<nsMenuX> kungFuDeathGrip(this);
  if (mObserver) {
    mObserver->OnMenuWillOpen(aPopupElement);
  }
}

void nsMenuX::OnMenuDidOpen(dom::Element* aPopupElement) {
  RefPtr<nsMenuX> kungFuDeathGrip(this);
  if (mObserver) {
    mObserver->OnMenuDidOpen(aPopupElement);
  }
}

void nsMenuX::OnMenuWillActivateItem(dom::Element* aPopupElement,
                                     dom::Element* aMenuItemElement) {
  RefPtr<nsMenuX> kungFuDeathGrip(this);
  if (mObserver) {
    mObserver->OnMenuWillActivateItem(aPopupElement, aMenuItemElement);
  }
}

void nsMenuX::OnMenuClosed(dom::Element* aPopupElement) {
  RefPtr<nsMenuX> kungFuDeathGrip(this);
  if (mObserver) {
    mObserver->OnMenuClosed(aPopupElement);
  }
}

void nsMenuX::AddMenuChild(const MenuChild& aChild) {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  WillInsertChild(aChild);
  mMenuChildren.AppendElement(aChild);

  bool isVisible = aChild.match(
      [](const RefPtr<nsMenuX>& aMenu) { return aMenu->IsVisible(); },
      [](const RefPtr<nsMenuItemX>& aMenuItem) {
        return aMenuItem->IsVisible();
      });
  NSMenuItem* nativeItem = aChild.match(
      [](const RefPtr<nsMenuX>& aMenu) { return aMenu->NativeNSMenuItem(); },
      [](const RefPtr<nsMenuItemX>& aMenuItem) {
        return aMenuItem->NativeNSMenuItem();
      });

  if (isVisible) {
    RemovePlaceholderIfPresent();
    [mNativeMenu addItem:nativeItem];
    ++mVisibleItemsCount;
    if (aChild.is<RefPtr<nsMenuItemX>>()) {
      aChild.as<RefPtr<nsMenuItemX>>()->CopyUserKeyEquivalentToKeyElement();
    }
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuX::CopyAllUserKeyEquivalentsToKeyElements() {
  for (const MenuChild& child : mMenuChildren) {
    child.match(
        [](const RefPtr<nsMenuX>& aMenu) {
          aMenu->CopyAllUserKeyEquivalentsToKeyElements();
        },
        [](const RefPtr<nsMenuItemX>& aMenuItem) {
          aMenuItem->CopyUserKeyEquivalentToKeyElement();
        });
  }
}

void nsMenuX::InsertMenuChild(const MenuChild& aChild) {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  WillInsertChild(aChild);
  size_t insertionIndex = FindInsertionIndex(aChild);
  mMenuChildren.InsertElementAt(insertionIndex, aChild);

  bool isVisible = aChild.match(
      [](const RefPtr<nsMenuX>& aMenu) { return aMenu->IsVisible(); },
      [](const RefPtr<nsMenuItemX>& aMenuItem) {
        return aMenuItem->IsVisible();
      });
  if (isVisible) {
    MenuChildChangedVisibility(aChild, true);
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuX::RemoveMenuChild(const MenuChild& aChild) {
  bool isVisible = aChild.match(
      [](const RefPtr<nsMenuX>& aMenu) { return aMenu->IsVisible(); },
      [](const RefPtr<nsMenuItemX>& aMenuItem) {
        return aMenuItem->IsVisible();
      });
  if (isVisible) {
    MenuChildChangedVisibility(aChild, false);
  }

  WillRemoveChild(aChild);
  mMenuChildren.RemoveElement(aChild);
}

size_t nsMenuX::FindInsertionIndex(const MenuChild& aChild) {
  nsCOMPtr<nsIContent> menuPopup = GetMenuPopupContent();
  MOZ_RELEASE_ASSERT(menuPopup);

  RefPtr<nsIContent> insertedContent = GetMenuChildContent(aChild);

  MOZ_RELEASE_ASSERT(insertedContent->GetParent() == menuPopup);

  // Iterate over menuPopup's children (insertedContent's siblings) until we
  // encounter insertedContent. At the same time, keep track of the index in
  // mMenuChildren.
  size_t index = 0;
  for (nsIContent* child = menuPopup->GetFirstChild();
       child && index < mMenuChildren.Length();
       child = child->GetNextSibling()) {
    if (child == insertedContent) {
      break;
    }

    RefPtr<nsIContent> contentAtIndex =
        GetMenuChildContent(mMenuChildren[index]);
    if (child == contentAtIndex) {
      index++;
    }
  }

  return index;
}

// Includes all items, including hidden/collapsed ones
uint32_t nsMenuX::GetItemCount() { return mMenuChildren.Length(); }

// Includes all items, including hidden/collapsed ones
mozilla::Maybe<nsMenuX::MenuChild> nsMenuX::GetItemAt(uint32_t aPos) {
  if (aPos >= (uint32_t)mMenuChildren.Length()) {
    return {};
  }

  return Some(mMenuChildren[aPos]);
}

// Only includes visible items
nsresult nsMenuX::GetVisibleItemCount(uint32_t& aCount) {
  aCount = mVisibleItemsCount;
  return NS_OK;
}

// Only includes visible items. Note that this is provides O(N) access
// If you need to iterate or search, consider using GetItemAt and doing your own
// filtering
Maybe<nsMenuX::MenuChild> nsMenuX::GetVisibleItemAt(uint32_t aPos) {
  uint32_t count = mMenuChildren.Length();
  if (aPos >= mVisibleItemsCount || aPos >= count) {
    return {};
  }

  // If there are no invisible items, can provide direct access
  if (mVisibleItemsCount == count) {
    return GetItemAt(aPos);
  }

  // Otherwise, traverse the array until we find the item we're looking for.
  uint32_t visibleNodeIndex = 0;
  for (uint32_t i = 0; i < count; i++) {
    MenuChild item = *GetItemAt(i);
    RefPtr<nsIContent> content = GetMenuChildContent(item);
    if (!nsMenuUtilsX::NodeIsHiddenOrCollapsed(content)) {
      if (aPos == visibleNodeIndex) {
        // we found the visible node we're looking for, return it
        return Some(item);
      }
      visibleNodeIndex++;
    }
  }

  return {};
}

Maybe<nsMenuX::MenuChild> nsMenuX::GetItemForElement(
    Element* aMenuChildElement) {
  for (auto& child : mMenuChildren) {
    RefPtr<nsIContent> content = GetMenuChildContent(child);
    if (content == aMenuChildElement) {
      return Some(child);
    }
  }
  return {};
}

nsresult nsMenuX::RemoveAll() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  [mNativeMenu removeAllItems];

  for (auto& child : mMenuChildren) {
    WillRemoveChild(child);
  }

  mMenuChildren.Clear();
  mVisibleItemsCount = 0;
  mIsPullDownPlaceholderPresent = false;

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

void nsMenuX::WillInsertChild(const MenuChild& aChild) {
  if (aChild.is<RefPtr<nsMenuX>>()) {
    aChild.as<RefPtr<nsMenuX>>()->SetObserver(this);
  }
}

void nsMenuX::WillRemoveChild(const MenuChild& aChild) {
  aChild.match(
      [](const RefPtr<nsMenuX>& aMenu) {
        aMenu->DetachFromGroupOwnerRecursive();
        aMenu->DetachFromParent();
        aMenu->SetObserver(nullptr);
      },
      [](const RefPtr<nsMenuItemX>& aMenuItem) {
        aMenuItem->DetachFromGroupOwner();
        aMenuItem->DetachFromParent();
      });
}

void nsMenuX::MenuOpened() {
  if (mIsOpen) {
    return;
  }

  // Make sure we fire any pending popupshown / popuphiding / popuphidden events
  // first.
  FlushMenuOpenedRunnable();
  FlushMenuClosedRunnable();

  if (!mDidFirePopupshowingAndIsApprovedToOpen) {
    // Fire popupshowing now.
    bool approvedToOpen = OnOpen();
    if (!approvedToOpen) {
      // We can only stop menus from opening which we open ourselves. We cannot
      // stop menubar root menus or menu submenus from opening. For context
      // menus, we can call OnOpen() before we ask the system to open the menu.
      NS_WARNING("The popupshowing event had preventDefault() called on it, "
                 "but in MenuOpened() it "
                 "is too late to stop the menu from opening.");
    }
  }

  mIsOpen = true;

  // Reset mDidFirePopupshowingAndIsApprovedToOpen for the next menu opening.
  mDidFirePopupshowingAndIsApprovedToOpen = false;

  if (mNeedsRebuild) {
    OnHighlightedItemChanged(Nothing());
    RemoveAll();
    RebuildMenu();
  }

  // Fire the popupshown event in MenuOpenedAsync.
  // MenuOpened() is called during menuWillOpen, and if cancelTracking is called
  // now, menuDidClose will not be called. The runnable object must not hold a
  // strong reference to the nsMenuX, so that there is no reference cycle.
  class MenuOpenedAsyncRunnable final : public mozilla::CancelableRunnable {
   public:
    explicit MenuOpenedAsyncRunnable(nsMenuX* aMenu)
        : CancelableRunnable("MenuOpenedAsyncRunnable"), mMenu(aMenu) {}

    // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230, bug 1535398)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult Run() override {
      if (RefPtr<nsMenuX> menu = mMenu) {
        menu->MenuOpenedAsync();
        mMenu = nullptr;
      }
      return NS_OK;
    }
    nsresult Cancel() override {
      mMenu = nullptr;
      return NS_OK;
    }

   private:
    nsMenuX* mMenu;  // weak, cleared by Cancel() and Run()
  };
  mPendingAsyncMenuOpenRunnable = new MenuOpenedAsyncRunnable(this);
  NS_DispatchToCurrentThread(mPendingAsyncMenuOpenRunnable);
}

void nsMenuX::FlushMenuOpenedRunnable() {
  if (mPendingAsyncMenuOpenRunnable) {
    MenuOpenedAsync();
  }
}

void nsMenuX::MenuOpenedAsync() {
  if (mPendingAsyncMenuOpenRunnable) {
    mPendingAsyncMenuOpenRunnable->Cancel();
    mPendingAsyncMenuOpenRunnable = nullptr;
  }

  mIsOpenForGecko = true;

  // Open the node.
  if (mContent->IsElement()) {
    mContent->AsElement()->SetAttr(kNameSpaceID_None, nsGkAtoms::open,
                                   u"true"_ns, true, IsKnownNewAttr::No);
  }

  RefPtr<nsIContent> popupContent = GetMenuPopupContent();

  // Notify our observer.
  if (mObserver && popupContent) {
    mObserver->OnMenuDidOpen(popupContent->AsElement());
  }

  // Fire popupshown.
  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetMouseEvent event(true, eXULPopupShown, nullptr,
                         WidgetMouseEvent::eReal);
  RefPtr<nsIContent> dispatchTo = popupContent ? popupContent : mContent;
  EventDispatcher::Dispatch(dispatchTo, nullptr, &event, nullptr, &status);
}

void nsMenuX::MenuClosed() {
  if (!mIsOpen) {
    return;
  }

  // Make sure we fire any pending popupshown events first.
  FlushMenuOpenedRunnable();

  // If any of our submenus were opened programmatically, make sure they get
  // closed first.
  for (auto& child : mMenuChildren) {
    if (child.is<RefPtr<nsMenuX>>()) {
      child.as<RefPtr<nsMenuX>>()->MenuClosed();
    }
  }

  mIsOpen = false;

  // Do the rest of the MenuClosed work in MenuClosedAsync.
  // MenuClosed() is called from -[NSMenuDelegate menuDidClose:]. If a menuitem
  // was clicked, menuDidClose is called *before* menuItemHit for the clicked
  // menu item is called. This runnable will be canceled if ~nsMenuX runs before
  // the runnable. The runnable object must not hold a strong reference to the
  // nsMenuX, so that there is no reference cycle.
  class MenuClosedAsyncRunnable final : public mozilla::CancelableRunnable {
   public:
    explicit MenuClosedAsyncRunnable(nsMenuX* aMenu)
        : CancelableRunnable("MenuClosedAsyncRunnable"), mMenu(aMenu) {}

    // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230, bug 1535398)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult Run() override {
      if (RefPtr<nsMenuX> menu = mMenu) {
        menu->MenuClosedAsync();
        mMenu = nullptr;
      }
      return NS_OK;
    }
    nsresult Cancel() override {
      mMenu = nullptr;
      return NS_OK;
    }

   private:
    nsMenuX* mMenu;  // weak, cleared by Cancel() and Run()
  };

  mPendingAsyncMenuCloseRunnable = new MenuClosedAsyncRunnable(this);

  NS_DispatchToCurrentThread(mPendingAsyncMenuCloseRunnable);
}

void nsMenuX::FlushMenuClosedRunnable() {
  // If any of our submenus have a pending menu closed runnable, make sure those
  // run first.
  for (auto& child : mMenuChildren) {
    if (child.is<RefPtr<nsMenuX>>()) {
      child.as<RefPtr<nsMenuX>>()->FlushMenuClosedRunnable();
    }
  }

  if (mPendingAsyncMenuCloseRunnable) {
    MenuClosedAsync();
  }
}

void nsMenuX::MenuClosedAsync() {
  if (mPendingAsyncMenuCloseRunnable) {
    mPendingAsyncMenuCloseRunnable->Cancel();
    mPendingAsyncMenuCloseRunnable = nullptr;
  }

  // If we have pending command events, run those first.
  nsTArray<PendingCommandEvent> events = std::move(mPendingCommandEvents);
  for (auto& event : events) {
    event.mMenuItem->DoCommand(event.mModifiers, event.mButton);
  }

  // Make sure no item is highlighted.
  OnHighlightedItemChanged(Nothing());

  nsCOMPtr<nsIContent> popupContent = GetMenuPopupContent();
  nsCOMPtr<nsIContent> dispatchTo = popupContent ? popupContent : mContent;

  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetMouseEvent popupHiding(true, eXULPopupHiding, nullptr,
                               WidgetMouseEvent::eReal);
  EventDispatcher::Dispatch(dispatchTo, nullptr, &popupHiding, nullptr,
                            &status);
  // The status is ignored because the native menu has already closed; default
  // behavior cannot be prevented for native menus.

  mIsOpenForGecko = false;

  if (mContent->IsElement()) {
    mContent->AsElement()->UnsetAttr(kNameSpaceID_None, nsGkAtoms::open, true);
  }

  status = nsEventStatus_eIgnore;
  WidgetMouseEvent popupHidden(true, eXULPopupHidden, nullptr,
                               WidgetMouseEvent::eReal);
  EventDispatcher::Dispatch(dispatchTo, nullptr, &popupHidden, nullptr,
                            &status);

  // Notify our observer.
  if (mObserver && popupContent) {
    mObserver->OnMenuClosed(popupContent->AsElement());
  }
}

void nsMenuX::ActivateItemAfterClosing(RefPtr<nsMenuItemX>&& aItem,
                                       NSEventModifierFlags aModifiers,
                                       int16_t aButton) {
  if (mIsOpenForGecko) {
    // Queue the event into mPendingCommandEvents. We will call aItem->DoCommand
    // in MenuClosedAsync(). We rely on the assumption that MenuClosedAsync will
    // run soon.
    mPendingCommandEvents.AppendElement(
        PendingCommandEvent{std::move(aItem), aModifiers, aButton});
  } else {
    // The menu item was activated outside of a regular open / activate / close
    // sequence. This happens in multiple cases:
    //  - When a menu item is activated by a keyboard shortcut while all windows
    //  are closed
    //    (otherwise those shortcuts go through Gecko's manual keyboard
    //    handling)
    //  - When a menu item in the Dock menu is clicked
    //  - During native menu tests
    //
    // Run the command synchronously.
    aItem->DoCommand(aModifiers, aButton);
  }
}

bool nsMenuX::Close() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (mDidFirePopupshowingAndIsApprovedToOpen && !mIsOpen) {
    // Close is being called right after this menu was opened, but before
    // MenuOpened() had a chance to run. Call it here so that we can go through
    // the entire popupshown -> popuphiding -> popuphidden sequence. Some
    // callers expect to get a popuphidden event even if they close the popup
    // before it was fully open.
    MenuOpened();
  }

  FlushMenuOpenedRunnable();

  bool wasOpen = mIsOpenForGecko;

  if (mIsOpen) {
    // Close the menu.
    // We usually don't get here during normal Firefox usage: If the user closes
    // the menu by clicking an item, or by clicking outside the menu, or by
    // pressing escape, then the menu gets closed by macOS, and not by a call to
    // nsMenuX::Close(). If we do get here, it's usually because we're running
    // an automated test. Close the menu without the fade-out animation so that
    // we don't unnecessarily slow down the automated tests.
    [mNativeMenu cancelTrackingWithoutAnimation];
    MOZMenuOpeningCoordinator.needToUnwindForMenuClosing = YES;

    // Handle closing synchronously.
    MenuClosed();
  }

  FlushMenuClosedRunnable();

  return wasOpen;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

void nsMenuX::OnHighlightedItemChanged(
    const Maybe<uint32_t>& aNewHighlightedIndex) {
  Maybe<uint32_t> newIndex = aNewHighlightedIndex;
  if (mIsPullDownPlaceholderPresent && newIndex) {
    if (newIndex.ref() > 0) {
      // Account for the pulldown placeholder item.
      newIndex.ref()--;
    } else {
      // Index 0 is the placeholder itself, not a real item.
      newIndex = Nothing();
    }
  }

  if (mHighlightedItemIndex == newIndex) {
    return;
  }

  if (mHighlightedItemIndex) {
    Maybe<nsMenuX::MenuChild> target = GetVisibleItemAt(*mHighlightedItemIndex);
    if (target && target->is<RefPtr<nsMenuItemX>>()) {
      bool handlerCalledPreventDefault;  // but we don't actually care
      target->as<RefPtr<nsMenuItemX>>()->DispatchDOMEvent(
          u"DOMMenuItemInactive"_ns, &handlerCalledPreventDefault);
    }
  }
  if (newIndex) {
    Maybe<nsMenuX::MenuChild> target = GetVisibleItemAt(*newIndex);
    if (target && target->is<RefPtr<nsMenuItemX>>()) {
      bool handlerCalledPreventDefault;  // but we don't actually care
      target->as<RefPtr<nsMenuItemX>>()->DispatchDOMEvent(
          u"DOMMenuItemActive"_ns, &handlerCalledPreventDefault);
    }
  }
  mHighlightedItemIndex = newIndex;
}

void nsMenuX::OnWillActivateItem(NSMenuItem* aItem) {
  if (!mIsOpenForGecko) {
    return;
  }

  if (mMenuGroupOwner && mObserver) {
    nsMenuItemX* item =
        mMenuGroupOwner->GetMenuItemForCommandID(uint32_t(aItem.tag));
    if (item && item->Content()->IsElement()) {
      RefPtr<dom::Element> itemElement = item->Content()->AsElement();
      if (nsCOMPtr<nsIContent> popupContent = GetMenuPopupContent()) {
        mObserver->OnMenuWillActivateItem(popupContent->AsElement(),
                                          itemElement);
      }
    }
  }
}

// Flushes style.
static NSUserInterfaceLayoutDirection DirectionForElement(
    dom::Element* aElement) {
  // Get the direction from the computed style so that inheritance into submenus
  // is respected. aElement may not have a frame.
  RefPtr<const ComputedStyle> sc =
      nsComputedDOMStyle::GetComputedStyle(aElement);
  if (!sc) {
    return NSApp.userInterfaceLayoutDirection;
  }

  switch (sc->StyleVisibility()->mDirection) {
    case StyleDirection::Ltr:
      return NSUserInterfaceLayoutDirectionLeftToRight;
    case StyleDirection::Rtl:
      return NSUserInterfaceLayoutDirectionRightToLeft;
  }
}

void nsMenuX::RebuildMenu() {
  MOZ_RELEASE_ASSERT(mNeedsRebuild);
  gConstructingMenu = true;

  // Retrieve our menupopup.
  nsCOMPtr<nsIContent> menuPopup = GetMenuPopupContent();
  if (!menuPopup) {
    gConstructingMenu = false;
    return;
  }

  if (menuPopup->IsElement()) {
    mNativeMenu.userInterfaceLayoutDirection =
        DirectionForElement(menuPopup->AsElement());
  }

  // Iterate over the kids
  for (nsIContent* child = menuPopup->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (Maybe<MenuChild> menuChild = CreateMenuChild(child)) {
      AddMenuChild(*menuChild);
    }
  }  // for each menu item

  InsertPlaceholderIfNeeded();

  gConstructingMenu = false;
  mNeedsRebuild = false;
}

void nsMenuX::RefreshMenuChildren(const MenuChild& aChildInserted) {
  if (gConstructingMenu) {
    return;
  }

  gConstructingMenu = true;

  // Retrieve our menupopup.
  nsCOMPtr<nsIContent> menuPopup = GetMenuPopupContent();
  if (!menuPopup) {
    gConstructingMenu = false;
    return;
  }

  // We want to recreate the items for the content inserted and everything after
  // it. Find the inserted child's content node so we can skip DOM children that
  // precede it.
  RefPtr<nsIContent> insertedContent = GetMenuChildContent(aChildInserted);

  bool found = false;
  for (nsIContent* child = menuPopup->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (!found) {
      if (child == insertedContent) {
        found = true;
      } else {
        continue;
      }
    }
    if (!child->IsElement()) {
      continue;
    }
    if (Maybe<MenuChild> menuChild = GetItemForElement(child->AsElement())) {
      RemoveMenuChild(*menuChild);
    }
    if (Maybe<MenuChild> menuChild = CreateMenuChild(child)) {
      InsertMenuChild(*menuChild);
    }
  }

  gConstructingMenu = false;
}

void nsMenuX::InsertPlaceholderIfNeeded() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (mIsAnchoredPullDown && !mIsPullDownPlaceholderPresent) {
    // Create the placeholder item used by NSPopUpButtonCell with
    // pullsDown=true.
    NSMenuItem* item = [[GeckoNSMenuItem alloc] initWithTitle:@""
                                                       action:nil
                                                keyEquivalent:@""];
    item.enabled = false;
    [mNativeMenu insertItem:item atIndex:0];
    [item release];
    mIsPullDownPlaceholderPresent = true;
  } else if ([mNativeMenu numberOfItems] == 0) {
    MOZ_RELEASE_ASSERT(mVisibleItemsCount == 0);
    NSMenuItem* item = [[GeckoNSMenuItem alloc] initWithTitle:@""
                                                       action:nil
                                                keyEquivalent:@""];
    item.enabled = NO;
    item.view =
        [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 20, 1)] autorelease];
    [mNativeMenu addItem:item];
    [item release];
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuX::RemovePlaceholderIfPresent() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (mVisibleItemsCount == 0 && [mNativeMenu numberOfItems] == 1 &&
      !mIsPullDownPlaceholderPresent) {
    // Remove the placeholder.
    [mNativeMenu removeItemAtIndex:0];
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuX::SetRebuild(bool aNeedsRebuild) {
  if (!gConstructingMenu) {
    mNeedsRebuild = aNeedsRebuild;
    if (mParent && mParent->AsMenuBar()) {
      mParent->AsMenuBar()->SetNeedsRebuild();
    }
  }
}

void nsMenuX::SetTitle() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  mContent->AsElement()->GetAttr(nsGkAtoms::label, mLabel);
  NSString* newCocoaLabelString =
      nsMenuUtilsX::GetTruncatedCocoaLabel(mLabel, mContent);
  mNativeMenu.title = newCocoaLabelString;
  mNativeMenuItem.title = newCocoaLabelString;

  SetAttributedTitle();

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuX::SetAttributedTitle() {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!IsAnchoredPopUp()) {
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

nsresult nsMenuX::SetEnabled(bool aIsEnabled) {
  if (aIsEnabled != mIsEnabled) {
    // we always want to rebuild when this changes
    mIsEnabled = aIsEnabled;
    mNativeMenuItem.enabled = mIsEnabled;
  }
  return NS_OK;
}

nsresult nsMenuX::GetEnabled(bool* aIsEnabled) {
  NS_ENSURE_ARG_POINTER(aIsEnabled);
  *aIsEnabled = mIsEnabled;
  return NS_OK;
}

GeckoNSMenu* nsMenuX::CreateMenuWithGeckoString(const nsString& aMenuTitle,
                                                bool aShowServices) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSString* title = [NSString stringWithCharacters:(UniChar*)aMenuTitle.get()
                                            length:aMenuTitle.Length()];
  GeckoNSMenu* myMenu = [[GeckoNSMenu alloc] initWithTitle:title];
  myMenu.delegate = mMenuDelegate;

  // We don't want this menu to auto-enable menu items because then Cocoa
  // overrides our decisions and things get incorrectly enabled/disabled.
  myMenu.autoenablesItems = NO;

  // Only show "Services", "Autofill" and similar entries provided by macOS
  // if our caller wants them:
  myMenu.allowsContextMenuPlugIns = aShowServices;

  // we used to install Carbon event handlers here, but since NSMenu* doesn't
  // create its underlying MenuRef until just before display, we delay until
  // that happens. Now we install the event handlers when Cocoa notifies
  // us that a menu is about to display - see the Cocoa MenuDelegate class.

  return myMenu;

  NS_OBJC_END_TRY_BLOCK_RETURN(nullptr);
}

Maybe<nsMenuX::MenuChild> nsMenuX::CreateMenuChild(nsIContent* aContent) {
  if (aContent->IsAnyOfXULElements(nsGkAtoms::menucaption, nsGkAtoms::menuitem,
                                   nsGkAtoms::menuseparator)) {
    return Some(MenuChild(CreateMenuItem(aContent)));
  }
  if (aContent->IsXULElement(nsGkAtoms::menu)) {
    return Some(
        MenuChild(MakeRefPtr<nsMenuX>(this, mMenuGroupOwner, aContent)));
  }
  return {};
}

RefPtr<nsMenuItemX> nsMenuX::CreateMenuItem(nsIContent* aMenuItemContent) {
  MOZ_RELEASE_ASSERT(aMenuItemContent);

  nsAutoString menuitemName;
  if (aMenuItemContent->IsElement()) {
    aMenuItemContent->AsElement()->GetAttr(nsGkAtoms::label, menuitemName);
  }

  EMenuItemType itemType = eRegularMenuItemType;
  if (aMenuItemContent->IsXULElement(nsGkAtoms::menuseparator)) {
    itemType = eSeparatorMenuItemType;
  } else if (aMenuItemContent->IsElement()) {
    static Element::AttrValuesArray strings[] = {nsGkAtoms::checkbox,
                                                 nsGkAtoms::radio, nullptr};
    switch (aMenuItemContent->AsElement()->FindAttrValueIn(
        kNameSpaceID_None, nsGkAtoms::type, strings, eCaseMatters)) {
      case 0:
        itemType = eCheckboxMenuItemType;
        break;
      case 1:
        itemType = eRadioMenuItemType;
        break;
    }
  }

  return MakeRefPtr<nsMenuItemX>(this, menuitemName, itemType, mMenuGroupOwner,
                                 aMenuItemContent);
}

// This menu is about to open. Returns false if the handler wants to stop the
// opening of the menu.
bool nsMenuX::OnOpen() {
  if (mDidFirePopupshowingAndIsApprovedToOpen) {
    return true;
  }

  if (mIsOpen) {
    NS_WARNING("nsMenuX::OnOpen() called while the menu is already considered "
               "to be open. This "
               "seems odd.");
  }

  RefPtr<nsIContent> popupContent = GetMenuPopupContent();

  if (mObserver && popupContent) {
    mObserver->OnMenuWillOpen(popupContent->AsElement());
  }

  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetMouseEvent event(true, eXULPopupShowing, nullptr,
                         WidgetMouseEvent::eReal);

  nsresult rv = NS_OK;
  RefPtr<nsIContent> dispatchTo = popupContent ? popupContent : mContent;
  rv = EventDispatcher::Dispatch(dispatchTo, nullptr, &event, nullptr, &status);
  if (NS_FAILED(rv) || status == nsEventStatus_eConsumeNoDefault) {
    return false;
  }

  DidFirePopupShowing();

  return true;
}

void nsMenuX::DidFirePopupShowing() {
  mDidFirePopupshowingAndIsApprovedToOpen = true;

  // If the open is going to succeed we need to walk our menu items, checking to
  // see if any of them have a command attribute. If so, several attributes
  // must potentially be updated.

  nsCOMPtr<nsIContent> popupContent = GetMenuPopupContent();
  if (!popupContent) {
    return;
  }

  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (pm) {
    pm->UpdateMenuItems(popupContent->AsElement());
  }
}

// Find the |menupopup| child in the |popup| representing this menu. It should
// be one of a very few children so we won't be iterating over a bazillion menu
// items to find it (so the strcmp won't kill us).
already_AddRefed<nsIContent> nsMenuX::GetMenuPopupContent() {
  // Check to see if we are a "menupopup" node (if we are a native menu).
  if (mContent->IsXULElement(nsGkAtoms::menupopup)) {
    return do_AddRef(mContent);
  }

  // Otherwise check our child nodes.

  for (RefPtr<nsIContent> child = mContent->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsXULElement(nsGkAtoms::menupopup)) {
      return child.forget();
    }
  }

  return nullptr;
}

bool nsMenuX::IsXULHelpMenu(nsIContent* aMenuContent) {
  bool retval = false;
  if (aMenuContent && aMenuContent->IsElement()) {
    nsAutoString id;
    aMenuContent->AsElement()->GetAttr(nsGkAtoms::id, id);
    if (id.Equals(u"helpMenu"_ns)) {
      retval = true;
    }
  }
  return retval;
}

bool nsMenuX::IsXULWindowMenu(nsIContent* aMenuContent) {
  bool retval = false;
  if (aMenuContent && aMenuContent->IsElement()) {
    nsAutoString id;
    aMenuContent->AsElement()->GetAttr(nsGkAtoms::id, id);
    if (id.Equals(u"windowMenu"_ns)) {
      retval = true;
    }
  }
  return retval;
}

bool nsMenuX::IsXULEditMenu(nsIContent* aMenuContent) {
  bool retval = false;
  if (aMenuContent && aMenuContent->IsElement()) {
    nsAutoString id;
    aMenuContent->AsElement()->GetAttr(nsGkAtoms::id, id);
    if (id.Equals(u"edit-menu"_ns)) {
      retval = true;
    }
  }
  return retval;
}

//
// nsChangeObserver
//

void nsMenuX::ObserveAttributeChanged(dom::Document* aDocument,
                                      nsIContent* aContent,
                                      nsAtom* aAttribute) {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // ignore the |open| attribute, which is by far the most common
  if (gConstructingMenu || (aAttribute == nsGkAtoms::open)) {
    return;
  }

  if (aAttribute == nsGkAtoms::disabled) {
    SetEnabled(!mContent->AsElement()->GetBoolAttr(nsGkAtoms::disabled));
  } else if (aAttribute == nsGkAtoms::label) {
    SetTitle();
  } else if (aAttribute == nsGkAtoms::hidden ||
             aAttribute == nsGkAtoms::collapsed) {
    SetRebuild(true);

    bool newVisible = !nsMenuUtilsX::NodeIsHiddenOrCollapsed(mContent);

    // don't do anything if the state is correct already
    if (newVisible == mVisible) {
      return;
    }

    mVisible = newVisible;
    if (mParent) {
      RefPtr<nsMenuX> self = this;
      mParent->MenuChildChangedVisibility(MenuChild(self), newVisible);
    }
    if (mVisible) {
      SetupIcon();
    }
  } else if (aAttribute == nsGkAtoms::image) {
    SetupIcon();
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void nsMenuX::ObserveContentRemoved(dom::Document* aDocument,
                                    nsIContent* aContainer,
                                    nsIContent* aChild) {
  if (gConstructingMenu) {
    return;
  }

  SetRebuild(true);
  if (mMenuGroupOwner) {
    mMenuGroupOwner->UnregisterForContentChanges(aChild);
  }

  if (!mIsOpen) {
    // We will update the menu contents the next time the menu is opened.
    return;
  }

  // The menu is currently open. Remove the child from mMenuChildren and from
  // our NSMenu.
  nsCOMPtr<nsIContent> popupContent = GetMenuPopupContent();
  if (popupContent && aContainer == popupContent && aChild->IsElement()) {
    if (Maybe<MenuChild> child = GetItemForElement(aChild->AsElement())) {
      RemoveMenuChild(*child);
    }
  }
}

void nsMenuX::ObserveContentInserted(dom::Document* aDocument,
                                     nsIContent* aContainer,
                                     nsIContent* aChild) {
  if (gConstructingMenu) {
    return;
  }

  SetRebuild(true);

  if (!mIsOpen) {
    // We will update the menu contents the next time the menu is opened.
    return;
  }

  // The menu is currently open. Insert the child into mMenuChildren and into
  // our NSMenu.
  nsCOMPtr<nsIContent> popupContent = GetMenuPopupContent();
  if (popupContent && aContainer == popupContent) {
    if (Maybe<MenuChild> child = CreateMenuChild(aChild)) {
      InsertMenuChild(*child);
      // NSMenu does not properly handle menu items being inserted while a menu
      // is open. On some versions of macOS (at least macOS 15 and 26), doing so
      // will clobber the item added and the existing items after it. Recreating
      // the menu item after insertion, and every menu item after it, works
      // around this issue. See bug 1993731 as an example.
      RefreshMenuChildren(*child);
    }
  }
}

void nsMenuX::SetupIcon() {
  mIcon->SetupIcon(mContent);
  mNativeMenuItem.image = mIcon->GetIconImage();
}

void nsMenuX::IconUpdated() {
  mNativeMenuItem.image = mIcon->GetIconImage();
  if (mIconListener) {
    mIconListener->IconUpdated();
  }
}

void nsMenuX::MenuChildChangedVisibility(const MenuChild& aChild,
                                         bool aIsVisible) {
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NSMenuItem* nativeItem = aChild.match(
      [](const RefPtr<nsMenuX>& aMenu) { return aMenu->NativeNSMenuItem(); },
      [](const RefPtr<nsMenuItemX>& aMenuItem) {
        return aMenuItem->NativeNSMenuItem();
      });
  if (aIsVisible) {
    MOZ_RELEASE_ASSERT(
        !nativeItem.menu,
        "The native item should not be in a menu while it is hidden");
    RemovePlaceholderIfPresent();
    NSInteger insertionPoint = CalculateNativeInsertionPoint(aChild);
    [mNativeMenu insertItem:nativeItem atIndex:insertionPoint];
    mVisibleItemsCount++;
    if (aChild.is<RefPtr<nsMenuItemX>>()) {
      aChild.as<RefPtr<nsMenuItemX>>()->CopyUserKeyEquivalentToKeyElement();
    }
  } else {
    MOZ_RELEASE_ASSERT(
        [mNativeMenu indexOfItem:nativeItem] != -1,
        "The native item should be in this menu while it is visible");
    [mNativeMenu removeItem:nativeItem];
    mVisibleItemsCount--;
    InsertPlaceholderIfNeeded();
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

NSInteger nsMenuX::CalculateNativeInsertionPoint(const MenuChild& aChild) {
  NSInteger insertionPoint = 0;
  for (auto& currItem : mMenuChildren) {
    if (currItem == aChild) {
      break;
    }
    NSMenuItem* nativeItem = currItem.match(
        [](const RefPtr<nsMenuX>& aMenu) { return aMenu->NativeNSMenuItem(); },
        [](const RefPtr<nsMenuItemX>& aMenuItem) {
          return aMenuItem->NativeNSMenuItem();
        });
    // Only count visible items.
    if (nativeItem.menu) {
      insertionPoint++;
    }
  }
  if (mIsPullDownPlaceholderPresent) {
    // Account for the pulldown placeholder item.
    insertionPoint++;
  }
  return insertionPoint;
}

void nsMenuX::Dump(uint32_t aIndent) const {
  printf(
      "%*s - menu [%p] %-16s <%s>", aIndent * 2, "", this,
      mLabel.IsEmpty() ? "(empty label)" : NS_ConvertUTF16toUTF8(mLabel).get(),
      NS_ConvertUTF16toUTF8(mContent->NodeName()).get());
  if (mNeedsRebuild) {
    printf(" [NeedsRebuild]");
  }
  if (mIsOpen) {
    printf(" [Open]");
  }
  if (mVisible) {
    printf(" [Visible]");
  }
  if (mIsEnabled) {
    printf(" [IsEnabled]");
  }
  printf(" (%d visible items)", int(mVisibleItemsCount));
  printf("\n");
  for (const auto& subitem : mMenuChildren) {
    subitem.match(
        [=](const RefPtr<nsMenuX>& aMenu) { aMenu->Dump(aIndent + 1); },
        [=](const RefPtr<nsMenuItemX>& aMenuItem) {
          aMenuItem->Dump(aIndent + 1);
        });
  }
}

//
// MenuDelegate Objective-C class, used to set up Carbon events
//

@implementation MenuDelegate

- (id)initWithGeckoMenu:(nsMenuX*)geckoMenu {
  if ((self = [super init])) {
    NS_ASSERTION(geckoMenu, "Cannot initialize native menu delegate with NULL "
                            "gecko menu! Will crash!");
    mGeckoMenu = geckoMenu;
    mBlocksToRunWhenOpen = [[NSMutableArray alloc] init];
  }
  return self;
}

- (void)dealloc {
  [mBlocksToRunWhenOpen release];
  [super dealloc];
}

- (void)runBlockWhenOpen:(void (^)())block {
  [mBlocksToRunWhenOpen addObject:[[block copy] autorelease]];
}

- (void)menu:(NSMenu*)aMenu willHighlightItem:(NSMenuItem*)aItem {
  if (!aMenu || !mGeckoMenu) {
    return;
  }

  Maybe<uint32_t> index;
  if (aItem) {
    NSInteger nativeIndex = [aMenu indexOfItem:aItem];
    if (nativeIndex != -1) {
      index = Some(static_cast<uint32_t>(nativeIndex));
    }
  }
  mGeckoMenu->OnHighlightedItemChanged(index);
}

- (void)menuWillOpen:(NSMenu*)menu {
  for (void (^block)() in mBlocksToRunWhenOpen) {
    block();
  }
  [mBlocksToRunWhenOpen removeAllObjects];

  if (!mGeckoMenu) {
    return;
  }

  // Don't do anything while the OS is (re)indexing our menus (on Leopard and
  // higher).  This stops the Help menu from being able to search in our
  // menus, but it also resolves many other problems.
  if (nsMenuX::sIndexingMenuLevel > 0) {
    return;
  }

  // Hold a strong reference to mGeckoMenu while calling its methods.
  RefPtr<nsMenuX> geckoMenu = mGeckoMenu;
  geckoMenu->MenuOpened();
}

- (void)menuDidClose:(NSMenu*)menu {
  if (!mGeckoMenu) {
    return;
  }

  // Don't do anything while the OS is (re)indexing our menus (on Leopard and
  // higher).  This stops the Help menu from being able to search in our
  // menus, but it also resolves many other problems.
  if (nsMenuX::sIndexingMenuLevel > 0) {
    return;
  }

  // Hold a strong reference to mGeckoMenu while calling its methods.
  RefPtr<nsMenuX> geckoMenu = mGeckoMenu;
  geckoMenu->MenuClosed();
}

// This is called after menuDidClose:.
- (void)menu:(NSMenu*)aMenu willActivateItem:(NSMenuItem*)aItem {
  if (!mGeckoMenu) {
    return;
  }

  // Hold a strong reference to mGeckoMenu while calling its methods.
  RefPtr<nsMenuX> geckoMenu = mGeckoMenu;
  geckoMenu->OnWillActivateItem(aItem);
}

@end

// MacOS Leopard (at least as of 10.5.2) has an obscure bug triggered by some
// behavior that's present in Mozilla.org browsers but not (as best I can
// tell) in Apple products like Safari.  (It's not yet clear exactly what this
// behavior is.)
//
// The bug is that sometimes you crash on quit in nsMenuX::RemoveAll(), on a
// call to [NSMenu removeItemAtIndex:].  The crash is caused by trying to
// access a deleted NSMenuItem object (sometimes (perhaps always?) by trying
// to send it a _setChangedFlags: message).  Though this object was deleted
// some time ago, it remains registered as a potential target for a particular
// key equivalent.  So when [NSMenu removeItemAtIndex:] removes the current
// target for that same key equivalent, the OS tries to "activate" the
// previous target.
//
// The underlying reason appears to be that NSMenu's _addItem:toTable: and
// _removeItem:fromTable: methods (which are used to keep a hashtable of
// registered key equivalents) don't properly "retain" and "release"
// NSMenuItem objects as they are added to and removed from the hashtable.
//
// Our (hackish) workaround is to shadow the OS's hashtable with another
// hastable of our own (gShadowKeyEquivDB), and use it to "retain" and
// "release" NSMenuItem objects as needed.  This resolves bmo bugs 422287 and
// 423669.  When (if) Apple fixes this bug, we can remove this workaround.

static NSMutableDictionary* gShadowKeyEquivDB = nil;

// Class for values in gShadowKeyEquivDB.

@interface KeyEquivDBItem : NSObject {
  NSMenuItem* mItem;
  NSMutableSet* mTables;
}

- (id)initWithItem:(NSMenuItem*)aItem table:(NSMapTable*)aTable;
- (BOOL)hasTable:(NSMapTable*)aTable;
- (int)addTable:(NSMapTable*)aTable;
- (int)removeTable:(NSMapTable*)aTable;

@end

@implementation KeyEquivDBItem

- (id)initWithItem:(NSMenuItem*)aItem table:(NSMapTable*)aTable {
  if (!gShadowKeyEquivDB) {
    gShadowKeyEquivDB = [[NSMutableDictionary alloc] init];
  }
  self = [super init];
  if (aItem && aTable) {
    mTables = [[NSMutableSet alloc] init];
    mItem = [aItem retain];
    [mTables addObject:[NSValue valueWithPointer:aTable]];
  } else {
    mTables = nil;
    mItem = nil;
  }
  return self;
}

- (void)dealloc {
  if (mTables) {
    [mTables release];
  }
  if (mItem) {
    [mItem release];
  }
  [super dealloc];
}

- (BOOL)hasTable:(NSMapTable*)aTable {
  return [mTables member:[NSValue valueWithPointer:aTable]] ? YES : NO;
}

// Does nothing if aTable (its index value) is already present in mTables.
- (int)addTable:(NSMapTable*)aTable {
  if (aTable) {
    [mTables addObject:[NSValue valueWithPointer:aTable]];
  }
  return [mTables count];
}

- (int)removeTable:(NSMapTable*)aTable {
  if (aTable) {
    NSValue* objectToRemove =
        [mTables member:[NSValue valueWithPointer:aTable]];
    if (objectToRemove) {
      [mTables removeObject:objectToRemove];
    }
  }
  return [mTables count];
}

@end

@interface NSMenu (MethodSwizzling)
+ (void)nsMenuX_NSMenu_addItem:(NSMenuItem*)aItem toTable:(NSMapTable*)aTable;
+ (void)nsMenuX_NSMenu_removeItem:(NSMenuItem*)aItem
                        fromTable:(NSMapTable*)aTable;
@end

@implementation NSMenu (MethodSwizzling)

+ (void)nsMenuX_NSMenu_addItem:(NSMenuItem*)aItem toTable:(NSMapTable*)aTable {
  if (aItem && aTable) {
    NSValue* key = [NSValue valueWithPointer:aItem];
    KeyEquivDBItem* shadowItem = [gShadowKeyEquivDB objectForKey:key];
    if (shadowItem) {
      [shadowItem addTable:aTable];
    } else {
      shadowItem = [[KeyEquivDBItem alloc] initWithItem:aItem table:aTable];
      [gShadowKeyEquivDB setObject:shadowItem forKey:key];
      // Release after [NSMutableDictionary setObject:forKey:] retains it (so
      // that it will get dealloced when removeObjectForKey: is called).
      [shadowItem release];
    }
  }

  [self nsMenuX_NSMenu_addItem:aItem toTable:aTable];
}

+ (void)nsMenuX_NSMenu_removeItem:(NSMenuItem*)aItem
                        fromTable:(NSMapTable*)aTable {
  [self nsMenuX_NSMenu_removeItem:aItem fromTable:aTable];

  if (aItem && aTable) {
    NSValue* key = [NSValue valueWithPointer:aItem];
    KeyEquivDBItem* shadowItem = [gShadowKeyEquivDB objectForKey:key];
    if (shadowItem && [shadowItem hasTable:aTable]) {
      if (![shadowItem removeTable:aTable]) {
        [gShadowKeyEquivDB removeObjectForKey:key];
      }
    }
  }
}

@end

// This class is needed to keep track of when the OS is (re)indexing all of
// our menus.  This appears to only happen on Leopard and higher, and can
// be triggered by opening the Help menu.  Some operations are unsafe while
// this is happening -- notably the calls to [[NSImage alloc]
// initWithSize:imageRect.size] and [newImage lockFocus] in nsMenuItemIconX::
// OnStopFrame().  But we don't yet have a complete list, and Apple doesn't
// yet have any documentation on this subject.  (Apple also doesn't yet have
// any documented way to find the information we seek here.)  The "original"
// of this class (the one whose indexMenuBarDynamically method we hook) is
// defined in the Shortcut framework in /System/Library/PrivateFrameworks.
@interface NSObject (SCTGRLIndexMethodSwizzling)
- (void)nsMenuX_SCTGRLIndex_indexMenuBarDynamically;
@end

@implementation NSObject (SCTGRLIndexMethodSwizzling)

- (void)nsMenuX_SCTGRLIndex_indexMenuBarDynamically {
  // This method appears to be called (once) whenever the OS (re)indexes our
  // menus.  sIndexingMenuLevel is a int32_t just in case it might be
  // reentered.  As it's running, it spawns calls to two undocumented
  // HIToolbox methods (_SimulateMenuOpening() and _SimulateMenuClosed()),
  // which "simulate" the opening and closing of our menus without actually
  // displaying them.
  ++nsMenuX::sIndexingMenuLevel;
  [self nsMenuX_SCTGRLIndex_indexMenuBarDynamically];
  --nsMenuX::sIndexingMenuLevel;
}

@end

@interface NSObject (NSServicesMenuUpdaterSwizzling)
- (void)nsMenuX_populateMenu:(NSMenu*)aMenu
          withServiceEntries:(NSArray*)aServices
                  forDisplay:(BOOL)aForDisplay;
@end

@interface _NSServiceEntry : NSObject
- (NSString*)bundleIdentifier;
@end

@implementation NSObject (NSServicesMenuUpdaterSwizzling)

- (void)nsMenuX_populateMenu:(NSMenu*)aMenu
          withServiceEntries:(NSArray*)aServices
                  forDisplay:(BOOL)aForDisplay {
  NSMutableArray* filteredServices = [NSMutableArray array];

  // We need to filter some services, such as "Search with Google", since this
  // service is duplicating functionality already exposed by our "Search Google
  // for..." context menu entry and because it opens in Safari, which can cause
  // confusion for users.
  for (_NSServiceEntry* service in aServices) {
    NSString* bundleId = [service bundleIdentifier];
    NSString* msg = [service valueForKey:@"message"];
    bool shouldSkip = ([bundleId isEqualToString:@"com.apple.Safari"]) ||
                      ([bundleId isEqualToString:@"com.apple.systemuiserver"] &&
                       [msg isEqualToString:@"openURL"]);
    if (!shouldSkip) {
      [filteredServices addObject:service];
    }
  }

  [self nsMenuX_populateMenu:aMenu
          withServiceEntries:filteredServices
                  forDisplay:aForDisplay];
}

@end
