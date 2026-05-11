/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Makes sure that the nested event loop for NSMenu tracking is situated as low
 * on the stack as possible, and that two NSMenu event loops are never nested.
 */

#include "MOZMenuOpeningCoordinator.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticPrefs_widget.h"

#include "nsCocoaFeatures.h"
#include "nsCocoaUtils.h"
#include "nsMenuX.h"
#include "nsObjCExceptions.h"

static BOOL sNeedToUnwindForMenuClosing = NO;

@interface MOZMenuOpeningInfo : NSObject
@property NSInteger handle;
@property(retain) NSMenu* menu;
@property NSPoint position;
@property(retain) NSView* view;
@property(retain) NSAppearance* appearance;
@property CGFloat fontSize;
@property BOOL isContextMenu;
@property BOOL isAnchoredMenu;
@property NSRect anchorRect;
@property NSRectEdge anchorEdge;
@property BOOL pullsDown;
@end

@implementation MOZMenuOpeningInfo
- (void)dealloc {
  [_menu release];
  [_view release];
  [_appearance release];
  [super dealloc];
}
@end

@implementation MOZMenuOpeningCoordinator {
  // non-nil between asynchronouslyOpenMenu:atScreenPosition:forView: and the
  // time at which it is unqueued in _runMenu.
  MOZMenuOpeningInfo* mPendingOpening;  // strong

  // An incrementing counter
  NSInteger mLastHandle;

  // YES while _runMenu is on the stack
  BOOL mRunMenuIsOnTheStack;
}

+ (instancetype)sharedInstance {
  static MOZMenuOpeningCoordinator* sInstance = nil;
  if (!sInstance) {
    sInstance = [[MOZMenuOpeningCoordinator alloc] init];
    mozilla::RunOnShutdown([&]() {
      [sInstance release];
      sInstance = nil;
    });
  }
  return sInstance;
}

- (void)dealloc {
  MOZ_RELEASE_ASSERT(!mPendingOpening, "should be empty at shutdown");
  [super dealloc];
}

- (NSInteger)asynchronouslyOpenMenu:(NSMenu*)aMenu
                   atScreenPosition:(NSPoint)aPosition
                            forView:(NSView*)aView
                     withAppearance:(NSAppearance*)aAppearance
                       withFontSize:(CGFloat)aFontSize
                      asContextMenu:(BOOL)aIsContextMenu
                     asAnchoredMenu:(BOOL)aIsAnchoredMenu
                         anchorRect:(NSRect)aAnchorRect
                         anchorEdge:(NSRectEdge)aAnchorEdge
                          pullsDown:(BOOL)aPullsDown {
  MOZ_RELEASE_ASSERT(!mPendingOpening,
                     "A menu is already waiting to open. Before opening the "
                     "next one, either wait "
                     "for this one to open or cancel the request.");

  NSInteger handle = ++mLastHandle;

  MOZMenuOpeningInfo* info = [[MOZMenuOpeningInfo alloc] init];
  info.handle = handle;
  info.menu = aMenu;
  info.position = aPosition;
  info.view = aView;
  info.appearance = aAppearance;
  info.fontSize = aFontSize;
  info.isContextMenu = aIsContextMenu;
  info.isAnchoredMenu = aIsAnchoredMenu;
  info.anchorRect = aAnchorRect;
  info.anchorEdge = aAnchorEdge;
  info.pullsDown = aPullsDown;
  mPendingOpening = info;

  if (!mRunMenuIsOnTheStack) {
    // Call _runMenu from the event loop, so that it doesn't block this call.
    [self performSelector:@selector(_runMenu) withObject:nil afterDelay:0.0];
  }

  return handle;
}

- (void)_runMenu {
  MOZ_RELEASE_ASSERT(!mRunMenuIsOnTheStack);

  mRunMenuIsOnTheStack = YES;

  while (mPendingOpening) {
    MOZMenuOpeningInfo* info = mPendingOpening;
    mPendingOpening = nil;

    @try {
      [self _openMenu:info.menu
          atScreenPosition:info.position
                   forView:info.view
            withAppearance:info.appearance
              withFontSize:info.fontSize
             asContextMenu:info.isContextMenu
            asAnchoredMenu:info.isAnchoredMenu
                anchorRect:info.anchorRect
                anchorEdge:info.anchorEdge
                 pullsDown:info.pullsDown];
    } @catch (NSException* exception) {
      nsObjCExceptionLog(exception);
    }

    [info release];

    // We have exited _openMenu's nested event loop.
    MOZMenuOpeningCoordinator.needToUnwindForMenuClosing = NO;
  }

  mRunMenuIsOnTheStack = NO;
}

- (void)cancelAsynchronousOpening:(NSInteger)aHandle {
  if (mPendingOpening && mPendingOpening.handle == aHandle) {
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(_runMenu)
                                               object:nil];
    [mPendingOpening release];
    mPendingOpening = nil;
  }
}

- (void)_openMenu:(NSMenu*)aMenu
    atScreenPosition:(NSPoint)aPosition
             forView:(NSView*)aView
      withAppearance:(NSAppearance*)aAppearance
        withFontSize:(CGFloat)aFontSize
       asContextMenu:(BOOL)aIsContextMenu
      asAnchoredMenu:(BOOL)aIsAnchoredMenu
          anchorRect:(NSRect)aAnchorRect
          anchorEdge:(NSRectEdge)aAnchorEdge
           pullsDown:(BOOL)aPullsDown {
  if (aAppearance) {
    if (@available(macOS 11.0, *)) {
      // By default, NSMenu inherits its appearance from the opening NSEvent's
      // window. If CSS has overridden it, on Big Sur + we can respect it with
      // -[NSMenu setAppearance].
      aMenu.appearance = aAppearance;
    }
  }
  if (aView && aIsAnchoredMenu) {
    // Create a NSPopUpButtonCell for our menu to anchor to.
    NSPopUpButtonCell* cell =
        [[NSPopUpButtonCell alloc] initTextCell:@"" pullsDown:aPullsDown];
    if (aPullsDown) {
      // Cocoa positions the menu relative to the button cell differently
      // depending on bezel style and OS version. Using this bezel style causes
      // Cocoa to place the menu as close to the button cell as possible across
      // OS versions.
      cell.bezelStyle = NSBezelStyleSmallSquare;
    } else {
      cell.altersStateOfSelectedItem = false;
    }
    cell.autoenablesItems = false;
    cell.menu = aMenu;
    cell.preferredEdge = aAnchorEdge;

    if (aFontSize > 0.f) {
      // Set the font size of the cell, if provided, so that the selected item's
      // checkmark image is sized more appropriately to menu item text.
      cell.font = [NSFont menuFontOfSize:aFontSize];
    }

    // Create the view that the menu will be anchored to
    NSView* anchorView = [[NSView alloc] initWithFrame:aAnchorRect];
    [aView addSubview:anchorView];
    [cell attachPopUpWithFrame:[anchorView bounds] inView:anchorView];

    // Invoke the popup
    [cell performClickWithFrame:[anchorView bounds] inView:anchorView];

    // Perform cleanup
    [cell release];
    [anchorView removeFromSuperview];
    [anchorView release];
  } else if (aView) {
    // There are multiple ways to display an NSMenu as a context menu.
    //
    //  1. We can return the NSMenu from -[ChildView menuForEvent:] and the
    //     NSView will open it for us.
    //  2. We can call +[NSMenu popUpContextMenu:withEvent:forView:] inside a
    //     mouseDown handler with a real mouse down event.
    //  3. We can call +[NSMenu popUpContextMenu:withEvent:forView:] at a later
    //     time, with a real mouse event that we stored earlier.
    //  4. We can call +[NSMenu popUpContextMenu:withEvent:forView:] at any
    //     time, with a synthetic mouse event that we create just for that
    //     purpose.
    //  5. We can call -[NSMenu popUpMenuPositioningItem:atLocation:inView:] and
    //     it just takes a position, not an event.
    //
    // 1-4 look the same, 5 looks different: 5 is made for use with
    // NSPopUpButton, where the selected item needs to be shown at a specific
    // position. If a tall menu is opened with a position close to the bottom
    // edge of the screen, 5 results in a cropped menu with scroll arrows, even
    // if the entire menu would fit on the screen, due to the positioning
    // constraint. 1-2 only work if the menu contents are known synchronously
    // during the call to menuForEvent or during the mouseDown event handler.
    // NativeMenuMac::ShowMenuAtPosition can be called at any time. It could be
    // called during a menuForEvent call (during a "contextmenu" event handler),
    // or during a mouseDown handler, or at a later time. The code below uses
    // option 4 as the preferred option for context menus because it's the
    // simplest: It works in all scenarios and it doesn't have the drawbacks of
    // option 5. For popups that aren't context menus and that should be
    // positioned as close as possible to the given screen position, we use
    // option 5.

    NSWindow* window = aView.window;
    NSPoint locationInWindow =
        nsCocoaUtils::ConvertPointFromScreen(window, aPosition);
    if (aIsContextMenu) {
      // Create a synthetic event at the right location and open the menu
      // [option 4].
      NSEvent* event =
          [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                             location:locationInWindow
                        modifierFlags:0
                            timestamp:NSProcessInfo.processInfo.systemUptime
                         windowNumber:window.windowNumber
                              context:nil
                          eventNumber:0
                           clickCount:1
                             pressure:0.0f];
      [NSMenu popUpContextMenu:aMenu withEvent:event forView:aView];
    } else {
      // For popups which are not context menus, we open the menu using [option
      // 5]. We pass `nil` to indicate that we're positioning the top left
      // corner of the menu. This path is used for anchored menupopups, so we
      // prefer option 5 over option 4 so that the menu doesn't get flipped if
      // space is tight.
      NSPoint locationInView = [aView convertPoint:locationInWindow
                                          fromView:nil];
      [aMenu popUpMenuPositioningItem:nil
                           atLocation:locationInView
                               inView:aView];
    }
  } else {
    // Open the menu using popUpMenuPositioningItem:atLocation:inView: [option
    // 5]. This is not preferred, because it positions the menu differently from
    // how a native context menu would be positioned; it enforces aPosition for
    // the top left corner even if this means that the menu will be displayed in
    // a clipped fashion with scroll arrows.
    [aMenu popUpMenuPositioningItem:nil atLocation:aPosition inView:nil];
  }
}

+ (void)setNeedToUnwindForMenuClosing:(BOOL)aValue {
  sNeedToUnwindForMenuClosing = aValue;
}

+ (BOOL)needToUnwindForMenuClosing {
  return sNeedToUnwindForMenuClosing;
}

@end
