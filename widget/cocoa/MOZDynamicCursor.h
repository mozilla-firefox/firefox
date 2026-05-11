/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZDynamicCursor_h_
#define MOZDynamicCursor_h_

#import <Cocoa/Cocoa.h>
#include "nsIWidget.h"

// MOZDynamicCursor.sharedInstance is a singleton NSCursor object whose
// underlying cursor can be changed at runtime.
// It can be used in an NSView cursorRect so that the system will call
// -[NSCursor set] on it at the right moments, for example when the
// mouse moves into a window or when the cursor needs to be set after
// a drag operation or when a context menu closes.
@interface MOZDynamicCursor : NSCursor {
 @private
  NSMutableDictionary* mCursors;
  NSCursor* mCurrentCursor;
  nsCursor mCurrentCursorType;
}

// Sets non-custom cursors and can be used as a fallback if setting
// a custom cursor did not succeed.
- (void)setNonCustomCursor:(const nsIWidget::Cursor&)aCursor;

// As above, but returns an error if the cursor isn't custom or we couldn't set
// it for some reason.
- (nsresult)setCustomCursor:(const nsIWidget::Cursor&)aCursor
          widgetScaleFactor:(CGFloat)aWidgetScaleFactor
                forceUpdate:(bool)aForceUpdate;

// Re-push our cached cursor state to AppKit. Used when AppKit may have
// changed cursor visibility under us without firing a mouse event that
// would re-evaluate cursor rects (e.g. macOS fullscreen window
// transitions, bug 2031413).
- (void)reassertCurrentCursor;

+ (MOZDynamicCursor*)sharedInstance;
@end

#endif  // MOZDynamicCursor_h_
