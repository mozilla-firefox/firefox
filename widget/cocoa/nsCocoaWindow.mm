/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCocoaWindow.h"

#include "nsISupportsPrimitives.h"
#include "nsArrayUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsDeviceContext.h"
#include "mozilla/dom/XULPopupElement.h"
#include "MOZDynamicCursor.h"
#include "nsIAppStartup.h"
#include "nsIDOMWindowUtils.h"
#include "nsILocalFileMac.h"
#include "CocoaCompositorWidget.h"
#include "GLContextCGL.h"
#include "MacThemeGeometryType.h"
#include "NativeMenuSupport.h"
#include "WindowRenderer.h"
#include "mozilla/Components.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/SwipeTracker.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/layers/APZInputBridge.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/NativeLayerCA.h"
#include "mozilla/widget/CompositorWidget.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/SurfacePool.h"
#include "mozilla/layers/IAPZCTreeManager.h"
#include "mozilla/dom/SimpleGestureEventBinding.h"
#include "mozilla/dom/WheelEventBinding.h"
#include "mozilla/ProfilerMarkers.h"
#include "NativeKeyBindings.h"
#include "ScreenHelperCocoa.h"
#include "TextInputHandler.h"
#include "nsCocoaUtils.h"
#include "nsObjCExceptions.h"
#include "nsCOMPtr.h"
#include "nsWidgetsCID.h"
#include "nsIRollupListener.h"
#include "nsChildView.h"
#include "nsWindowMap.h"
#include "nsAppShell.h"
#include "nsIAppShellService.h"
#include "nsIBaseWindow.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIAppWindow.h"
#include "nsToolkit.h"
#include "nsPIDOMWindow.h"
#include "nsThreadUtils.h"
#include "nsMenuBarX.h"
#include "nsMenuUtilsX.h"
#include "nsStyleConsts.h"
#include "nsLayoutUtils.h"
#include "nsDragService.h"
#include "nsNativeThemeColors.h"
#include "nsNativeThemeCocoa.h"
#include "nsClipboard.h"
#include "nsCocoaFeatures.h"
#include "nsIScreenManager.h"
#include "nsIWidgetListener.h"
#include "nsXULPopupManager.h"
#include "VibrancyManager.h"
#include "nsPresContext.h"
#include "nsDocShell.h"

#include "gfxPlatform.h"
#include "qcms.h"

#include "mozilla/AutoRestore.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/dom/Document.h"
#include "mozilla/Maybe.h"
#include "mozilla/NativeKeyBindingsType.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_general.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/WritingModes.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/widget/Screen.h"
#include <algorithm>

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/DocAccessible.h"
#endif

#undef DEBUG_UPDATE
#undef INVALIDATE_DEBUGGING  // flash areas as they are invalidated

namespace mozilla {
namespace layers {
class LayerManager;
}  // namespace layers
}  // namespace mozilla
using namespace mozilla::layers;
using namespace mozilla::widget;
using namespace mozilla::gl;
using namespace mozilla;

BOOL sTouchBarIsInitialized = NO;

// defined in nsMenuBarX.mm
extern NSMenu* sApplicationMenu;  // Application menu shared by all menubars
extern BOOL gSomeMenuBarPainted;

static uint32_t sModalWindowCount = 0;

LazyLogModule sCocoaLog("nsCocoaWidgets");

extern "C" {
// CGSPrivate.h
typedef NSInteger CGSConnection;
typedef NSUInteger CGSSpaceID;
typedef NSInteger CGSWindow;
typedef enum {
  kCGSSpaceIncludesCurrent = 1 << 0,
  kCGSSpaceIncludesOthers = 1 << 1,
  kCGSSpaceIncludesUser = 1 << 2,

  kCGSAllSpacesMask =
      kCGSSpaceIncludesCurrent | kCGSSpaceIncludesOthers | kCGSSpaceIncludesUser
} CGSSpaceMask;
static NSString* const CGSSpaceIDKey = @"ManagedSpaceID";
static NSString* const CGSSpacesKey = @"Spaces";
extern CGSConnection _CGSDefaultConnection(void);
extern CGError CGSSetWindowTransform(CGSConnection cid, CGSWindow wid,
                                     CGAffineTransform transform);
}

static void RollUpPopups(nsIRollupListener::AllowAnimations aAllowAnimations =
                             nsIRollupListener::AllowAnimations::Yes) {
  if (RefPtr pm = nsXULPopupManager::GetInstance()) {
    pm->RollupTooltips();
  }

  nsIRollupListener* rollupListener = nsIWidget::GetActiveRollupListener();
  if (!rollupListener) {
    return;
  }
  if (rollupListener->RollupNativeMenu()) {
    return;
  }
  nsCOMPtr<nsIWidget> rollupWidget = rollupListener->GetRollupWidget();
  if (!rollupWidget) {
    return;
  }
  nsIRollupListener::RollupOptions options{0, nullptr, aAllowAnimations};
  rollupListener->Rollup(options);
}

extern nsIArray* gDraggedTransferables;
ChildView* ChildViewMouseTracker::sLastMouseEventView = nil;
NSEvent* ChildViewMouseTracker::sLastMouseMoveEvent = nil;
NSWindow* ChildViewMouseTracker::sWindowUnderMouse = nil;
MOZ_RUNINIT NSPoint ChildViewMouseTracker::sLastScrollEventScreenLocation =
    NSZeroPoint;

#ifdef INVALIDATE_DEBUGGING
static void blinkRect(Rect* r);
static void blinkRgn(RgnHandle rgn);
#endif

bool gUserCancelledDrag = false;

uint32_t nsCocoaWindow::sLastInputEventCount = 0;

static bool sIsTabletPointerActivated = false;

static uint32_t sUniqueKeyEventId = 0;

// The view that will do our drawing or host our NSOpenGLContext or Core
// Animation layer.
@interface PixelHostingView : NSView {
}

@end

@interface ChildView (Private)

// sets up our view, attaching it to its owning gecko view
- (id)initWithFrame:(NSRect)inFrame geckoChild:(nsCocoaWindow*)inChild;

// set up a gecko mouse event based on a cocoa mouse event
- (void)convertCocoaMouseWheelEvent:(NSEvent*)aMouseEvent
                       toGeckoEvent:(WidgetWheelEvent*)outWheelEvent;
- (void)convertCocoaMouseEvent:(NSEvent*)aMouseEvent
                  toGeckoEvent:(WidgetInputEvent*)outGeckoEvent;
- (void)convertCocoaTabletPointerEvent:(NSEvent*)aMouseEvent
                          toGeckoEvent:(WidgetMouseEvent*)outGeckoEvent;
- (NSMenu*)contextMenu;

- (void)markLayerForDisplay;
- (CALayer*)rootCALayer;
- (void)updateRootCALayer;

#ifdef ACCESSIBILITY
- (id<mozAccessible>)accessible;
#endif

- (LayoutDeviceIntPoint)convertWindowCoordinates:(NSPoint)aPoint;
- (LayoutDeviceIntPoint)convertWindowCoordinatesRoundDown:(NSPoint)aPoint;

- (BOOL)inactiveWindowAcceptsMouseEvent:(NSEvent*)aEvent;
- (void)updateWindowDraggableState;

- (bool)beginOrEndGestureForEventPhase:(NSEvent*)aEvent;

@end

#pragma mark -

// Flips a screen coordinate from a point in the cocoa coordinate system
// (bottom-left rect) to a point that is a "flipped" cocoa coordinate system
// (starts in the top-left).
static inline void FlipCocoaScreenCoordinate(NSPoint& inPoint) {
  inPoint.y = nsCocoaUtils::FlippedScreenY(inPoint.y);
}

void nsCocoaWindow::TearDownView() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mChildView) return;

  NSWindow* win = [mChildView window];
  NSResponder* responder = [win firstResponder];

  // We're being unhooked from the view hierarchy, don't leave our view
  // or a child view as the window first responder.
  if (responder && [responder isKindOfClass:[NSView class]] &&
      [(NSView*)responder isDescendantOf:mChildView]) {
    [win makeFirstResponder:[mChildView superview]];
  }

  // If mView is win's contentView, win (mView's NSWindow) "owns" mView --
  // win has retained mView, and will detach it from the view hierarchy and
  // release it when necessary (when win is itself destroyed (in a call to
  // [win dealloc])).  So all we need to do here is call [mView release] (to
  // match the call to [mView retain] in nsChildView::StandardCreate()).
  // Also calling [mView removeFromSuperviewWithoutNeedingDisplay] causes
  // mView to be released again and dealloced, while remaining win's
  // contentView.  So if we do that here, win will (for a short while) have
  // an invalid contentView (for the consequences see bmo bugs 381087 and
  // 374260).
  if ([mChildView isEqual:[win contentView]]) {
    [mChildView release];
  } else {
    // Stop NSView hierarchy being changed during [ChildView drawRect:]
    [mChildView performSelectorOnMainThread:@selector(delayedTearDown)
                                 withObject:nil
                              waitUntilDone:false];
  }
  mChildView = nil;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

#if 0
static void PrintViewHierarchy(NSView *view)
{
  while (view) {
    NSLog(@"  view is %p, frame %@", view, NSStringFromRect([view frame]));
    view = [view superview];
  }
}
#endif

// Return native data according to aDataType

// Some NSView methods (e.g. setFrame and setHidden) invalidate the view's
// bounds in our window. However, we don't want these invalidations because
// they are unnecessary and because they actually slow us down since we
// block on the compositor inside drawRect.
// When we actually need something invalidated, there will be an explicit call
// to Invalidate from Gecko, so turning these automatic invalidations off
// won't hurt us in the non-OMTC case.
// The invalidations inside these NSView methods happen via a call to the
// private method -[NSWindow _setNeedsDisplayInRect:]. Our BaseWindow
// implementation of that method is augmented to let us ignore those calls
// using -[BaseWindow disable/enableSetNeedsDisplay].
static void ManipulateViewWithoutNeedingDisplay(NSView* aView,
                                                void (^aCallback)()) {
  BaseWindow* win = nil;
  if ([[aView window] isKindOfClass:[BaseWindow class]]) {
    win = (BaseWindow*)[aView window];
  }
  [win disableSetNeedsDisplay];
  aCallback();
  [win enableSetNeedsDisplay];
}

// Override to set the cursor on the mac
void nsCocoaWindow::SetCursor(const Cursor& aCursor) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if ([mChildView isDragInProgress]) {
    return;  // Don't change the cursor during dragging.
  }

  nsIWidget::SetCursor(aCursor);

  bool forceUpdate = mUpdateCursor;
  mUpdateCursor = false;
  if (mCustomCursorAllowed && NS_SUCCEEDED([MOZDynamicCursor.sharedInstance
                                    setCustomCursor:aCursor
                                  widgetScaleFactor:BackingScaleFactor()
                                        forceUpdate:forceUpdate])) {
    return;
  }

  [MOZDynamicCursor.sharedInstance setNonCustomCursor:aCursor];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// The following three methods are primarily an attempt to avoid glitches during
// window resizing.
// Here's some background on how these glitches come to be:
// CoreAnimation transactions are per-thread. They don't nest across threads.
// If you submit a transaction on the main thread and a transaction on a
// different thread, the two will race to the window server and show up on the
// screen in the order that they happen to arrive in at the window server.
// When the window size changes, there's another event that needs to be
// synchronized with: the window "shape" change. Cocoa has built-in
// synchronization mechanics that make sure that *main thread* window paints
// during window resizes are synchronized properly with the window shape change.
// But no such built-in synchronization exists for CATransactions that are
// triggered on a non-main thread. To cope with this, we define a "danger zone"
// during which we simply avoid triggering any CATransactions on a non-main
// thread (called "async" CATransactions here). This danger zone starts at the
// earliest opportunity at which we know about the size change, which is
// nsCocoaWindow::Resize, and ends at a point at which we know for sure that the
// paint has been handled completely, which is when we return to the event loop
// after layer display.
void nsCocoaWindow::SuspendAsyncCATransactions() {
  if (mUnsuspendAsyncCATransactionsRunnable) {
    mUnsuspendAsyncCATransactionsRunnable->Cancel();
    mUnsuspendAsyncCATransactionsRunnable = nullptr;
  }

  // Make sure that there actually will be a CATransaction on the main thread
  // during which we get a chance to schedule unsuspension. Otherwise we might
  // accidentally stay suspended indefinitely.
  [mChildView markLayerForDisplay];

  // Ensure that whatever we are going to do does sync flushes of the
  // rendering pipeline, giving us smooth animation.
  if (mCompositorBridgeChild) {
    mCompositorBridgeChild->SetForceSyncFlushRendering(true);
  }
  mNativeLayerRoot->SuspendOffMainThreadCommits();
}

void nsCocoaWindow::MaybeScheduleUnsuspendAsyncCATransactions() {
  if (mNativeLayerRoot->AreOffMainThreadCommitsSuspended() &&
      !mUnsuspendAsyncCATransactionsRunnable) {
    mUnsuspendAsyncCATransactionsRunnable = NewCancelableRunnableMethod(
        "nsCocoaWindow::MaybeScheduleUnsuspendAsyncCATransactions", this,
        &nsCocoaWindow::UnsuspendAsyncCATransactions);
    NS_DispatchToMainThread(mUnsuspendAsyncCATransactionsRunnable);
  }
}

void nsCocoaWindow::UnsuspendAsyncCATransactions() {
  mUnsuspendAsyncCATransactionsRunnable = nullptr;

  if (mNativeLayerRoot->UnsuspendOffMainThreadCommits()) {
    // We need to call mNativeLayerRoot->CommitToScreen() at the next available
    // opportunity.
    // The easiest way to handle this request is to mark the layer as needing
    // display, because this will schedule a main thread CATransaction, during
    // which HandleMainThreadCATransaction will call CommitToScreen().
    [mChildView markLayerForDisplay];
  }

  // We're done with our critical animation, so allow async flushes again.
  if (mCompositorBridgeChild) {
    mCompositorBridgeChild->SetForceSyncFlushRendering(false);
  }
}

nsresult nsCocoaWindow::SynthesizeNativeKeyEvent(
    int32_t aNativeKeyboardLayout, int32_t aNativeKeyCode,
    uint32_t aModifierFlags, const nsAString& aCharacters,
    const nsAString& aUnmodifiedCharacters,
    nsISynthesizedEventCallback* aCallback) {
  AutoSynthesizedEventCallbackNotifier notifier(aCallback);
  return mTextInputHandler->SynthesizeNativeKeyEvent(
      aNativeKeyboardLayout, aNativeKeyCode, aModifierFlags, aCharacters,
      aUnmodifiedCharacters);
}

nsresult nsCocoaWindow::SynthesizeNativeMouseEvent(
    LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
    MouseButton aButton, nsIWidget::Modifiers aModifierFlags,
    nsISynthesizedEventCallback* aCallback) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  NSPoint pt =
      nsCocoaUtils::DevPixelsToCocoaPoints(aPoint, BackingScaleFactor());

  // Move the mouse cursor to the requested position and reconnect it to the
  // mouse.
  CGWarpMouseCursorPosition(NSPointToCGPoint(pt));
  CGAssociateMouseAndMouseCursorPosition(true);

  // aPoint is given with the origin on the top left, but convertScreenToBase
  // expects a point in a coordinate system that has its origin on the bottom
  // left.
  NSPoint screenPoint = NSMakePoint(pt.x, nsCocoaUtils::FlippedScreenY(pt.y));
  NSPoint windowPoint =
      nsCocoaUtils::ConvertPointFromScreen([mChildView window], screenPoint);
  NSEventModifierFlags modifierFlags =
      nsCocoaUtils::ConvertWidgetModifiersToMacModifierFlags(aModifierFlags);

  if (aButton == MouseButton::eX1 || aButton == MouseButton::eX2) {
    // NSEvent has `buttonNumber` for `NSEventTypeOther*`.  However, it seems
    // that there is no way to specify it.  Therefore, we should return error
    // for now.
    return NS_ERROR_INVALID_ARG;
  }

  NSEventType nativeEventType;
  switch (aNativeMessage) {
    case NativeMouseMessage::ButtonDown:
    case NativeMouseMessage::ButtonUp: {
      switch (aButton) {
        case MouseButton::ePrimary:
          nativeEventType = aNativeMessage == NativeMouseMessage::ButtonDown
                                ? NSEventTypeLeftMouseDown
                                : NSEventTypeLeftMouseUp;
          break;
        case MouseButton::eMiddle:
          nativeEventType = aNativeMessage == NativeMouseMessage::ButtonDown
                                ? NSEventTypeOtherMouseDown
                                : NSEventTypeOtherMouseUp;
          break;
        case MouseButton::eSecondary:
          nativeEventType = aNativeMessage == NativeMouseMessage::ButtonDown
                                ? NSEventTypeRightMouseDown
                                : NSEventTypeRightMouseUp;
          break;
        default:
          return NS_ERROR_INVALID_ARG;
      }
      break;
    }
    case NativeMouseMessage::Move:
      nativeEventType = NSEventTypeMouseMoved;
      break;
    case NativeMouseMessage::EnterWindow:
      nativeEventType = NSEventTypeMouseEntered;
      break;
    case NativeMouseMessage::LeaveWindow:
      nativeEventType = NSEventTypeMouseExited;
      break;
  }

  NSEvent* event =
      [NSEvent mouseEventWithType:nativeEventType
                         location:windowPoint
                    modifierFlags:modifierFlags
                        timestamp:[[NSProcessInfo processInfo] systemUptime]
                     windowNumber:[[mChildView window] windowNumber]
                          context:nil
                      eventNumber:0
                       clickCount:1
                         pressure:0.0];

  if (!event) return NS_ERROR_FAILURE;

  if ([[mChildView window] isKindOfClass:[BaseWindow class]]) {
    // Tracking area events don't end up in their tracking areas when sent
    // through [NSApp sendEvent:], so pass them directly to the right methods.
    BaseWindow* window = (BaseWindow*)[mChildView window];
    if (nativeEventType == NSEventTypeMouseEntered) {
      [window mouseEntered:event];
      return NS_OK;
    }
    if (nativeEventType == NSEventTypeMouseExited) {
      [window mouseExited:event];
      return NS_OK;
    }
    if (nativeEventType == NSEventTypeMouseMoved) {
      [window mouseMoved:event];
      return NS_OK;
    }
  }

  [NSApp sendEvent:event];
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

nsresult nsCocoaWindow::SynthesizeNativeMouseMove(
    LayoutDeviceIntPoint aPoint, nsISynthesizedEventCallback* aCallback) {
  if (GetNativePointerLockedMode()) {
    AutoSynthesizedEventCallbackNotifier notifier(aCallback);
    sNativeLockedPoint = aPoint - WidgetToScreenOffset();

    WidgetMouseEvent event(true, eMouseMove, this, WidgetMouseEvent::eReal);
    event.mRefPoint = sNativeLockedPoint;
    event.mTimeStamp = nsCocoaUtils::GetEventTimeStamp(0);
    event.mMovement = Some(LayoutDeviceIntPoint(0, 0));
    DispatchInputEvent(&event);

    return NS_OK;
  }

  return SynthesizeNativeMouseEvent(
      aPoint, NativeMouseMessage::Move, mozilla::MouseButton::eNotPressed,
      nsIWidget::Modifiers::NO_MODIFIERS, aCallback);
}

nsresult nsCocoaWindow::SynthesizeNativeMouseScrollEvent(
    mozilla::LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage,
    double aDeltaX, double aDeltaY, double aDeltaZ, uint32_t aModifierFlags,
    uint32_t aAdditionalFlags, nsISynthesizedEventCallback* aCallback) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  NSPoint pt =
      nsCocoaUtils::DevPixelsToCocoaPoints(aPoint, BackingScaleFactor());

  // Move the mouse cursor to the requested position and reconnect it to the
  // mouse.
  CGWarpMouseCursorPosition(NSPointToCGPoint(pt));
  CGAssociateMouseAndMouseCursorPosition(true);

  // Mostly copied from http://stackoverflow.com/a/6130349
  CGScrollEventUnit units =
      (aAdditionalFlags & nsIDOMWindowUtils::MOUSESCROLL_SCROLL_LINES)
          ? kCGScrollEventUnitLine
          : kCGScrollEventUnitPixel;
  CGEventRef cgEvent = CGEventCreateScrollWheelEvent(
      NULL, units, 3, (int32_t)aDeltaY, (int32_t)aDeltaX, (int32_t)aDeltaZ);
  if (!cgEvent) {
    return NS_ERROR_FAILURE;
  }

  if (aNativeMessage) {
    CGEventSetIntegerValueField(cgEvent, kCGScrollWheelEventScrollPhase,
                                aNativeMessage);
  }

  // On macOS 10.14 and up CGEventPost won't work because of changes in macOS
  // to improve security. This code makes an NSEvent corresponding to the
  // wheel event and dispatches it directly to the scrollWheel handler. Some
  // fiddling is needed with the coordinates in order to simulate what macOS
  // would do; this code adapted from the Chromium equivalent function at
  // https://chromium.googlesource.com/chromium/src.git/+/62.0.3178.1/ui/events/test/cocoa_test_event_utils.mm#38
  CGPoint location = CGEventGetLocation(cgEvent);
  location.y += NSMinY([[mChildView window] frame]);
  location.x -= NSMinX([[mChildView window] frame]);
  CGEventSetLocation(cgEvent, location);

  uint64_t kNanosPerSec = 1000000000L;
  CGEventSetTimestamp(
      cgEvent, [[NSProcessInfo processInfo] systemUptime] * kNanosPerSec);

  NSEvent* event = [NSEvent eventWithCGEvent:cgEvent];
  [event setValue:[mChildView window] forKey:@"_window"];
  [mChildView scrollWheel:event];

  CFRelease(cgEvent);
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

nsresult nsCocoaWindow::SynthesizeNativeTouchPoint(
    uint32_t aPointerId, TouchPointerState aPointerState,
    mozilla::LayoutDeviceIntPoint aPoint, double aPointerPressure,
    uint32_t aPointerOrientation, nsISynthesizedEventCallback* aCallback) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  MOZ_ASSERT(NS_IsMainThread());
  if (aPointerState == TOUCH_HOVER) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!mSynthesizedTouchInput) {
    mSynthesizedTouchInput = MakeUnique<MultiTouchInput>();
  }

  LayoutDeviceIntPoint pointInWindow = aPoint - WidgetToScreenOffset();
  MultiTouchInput inputToDispatch = UpdateSynthesizedTouchState(
      mSynthesizedTouchInput.get(), TimeStamp::Now(), aPointerId, aPointerState,
      pointInWindow, aPointerPressure, aPointerOrientation);
  DispatchTouchInput(inputToDispatch);
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

nsresult nsCocoaWindow::SynthesizeNativeTouchpadDoubleTap(
    mozilla::LayoutDeviceIntPoint aPoint, uint32_t aModifierFlags) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  DispatchDoubleTapGesture(TimeStamp::Now(), aPoint - WidgetToScreenOffset(),
                           static_cast<Modifiers>(aModifierFlags));

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

bool nsCocoaWindow::SendEventToNativeMenuSystem(NSEvent* aEvent) {
  bool handled = false;
  if (nsMenuBarX* mb = GetMenuBar()) {
    // Check if main menu wants to handle the event.
    handled = mb->PerformKeyEquivalent(aEvent);
  }

  if (!handled && sApplicationMenu) {
    // Check if application menu wants to handle the event.
    handled = [sApplicationMenu performKeyEquivalent:aEvent];
  }

  return handled;
}

void nsCocoaWindow::PostHandleKeyEvent(mozilla::WidgetKeyboardEvent* aEvent) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  // We always allow keyboard events to propagate to keyDown: but if they are
  // not handled we give menu items a chance to act. This allows for handling of
  // custom shortcuts. Note that existing shortcuts cannot be reassigned yet and
  // will have been handled by keyDown: before we get here.
  NSMutableDictionary* nativeKeyEventsMap = [ChildView sNativeKeyEventsMap];
  NSEvent* cocoaEvent = [nativeKeyEventsMap objectForKey:@(aEvent->mUniqueId)];
  if (!cocoaEvent) {
    return;
  }

  // If the escape key is pressed, the expectations are as follows:
  // 1. If the page is loading, interrupt loading.
  // 2. Give a website an opportunity to handle the event and call
  //    preventDefault() on it.
  // 3. If the browser is fullscreen, we do not have fullscreen keyboard lock
  //    enabled, and the page isn't loading, exit fullscreen.
  // 4. Ignore.
  // Case 1 and 2 are handled before we get here. Below, we handle case 3.
  Document* doc = GetDocument();
  if (StaticPrefs::browser_fullscreen_exit_on_escape() &&
      [cocoaEvent keyCode] == kVK_Escape &&
      [[mChildView window] styleMask] & NSWindowStyleMaskFullScreen &&
      !(doc && doc->HasFullscreenKeyboardLockEnabled())) {
    [[mChildView window] toggleFullScreen:nil];
  }

  if (SendEventToNativeMenuSystem(cocoaEvent)) {
    aEvent->PreventDefault();
  }
  [nativeKeyEventsMap removeObjectForKey:@(aEvent->mUniqueId)];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Used for testing native menu system structure and event handling.
nsresult nsCocoaWindow::ActivateNativeMenuItemAt(const nsAString& indexString) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  nsMenuUtilsX::CheckNativeMenuConsistency([NSApp mainMenu]);

  NSString* locationString =
      [NSString stringWithCharacters:reinterpret_cast<const unichar*>(
                                         indexString.BeginReading())
                              length:indexString.Length()];
  NSMenuItem* item = nsMenuUtilsX::NativeMenuItemWithLocation(
      [NSApp mainMenu], locationString, true);
  // We can't perform an action on an item with a submenu, that will raise
  // an obj-c exception.
  if (item && ![item hasSubmenu]) {
    NSMenu* parent = [item menu];
    if (parent) {
      // NSLog(@"Performing action for native menu item titled: %@\n",
      //       [[currentSubmenu itemAtIndex:targetIndex] title]);
      mozilla::AutoRestore<bool> autoRestore(
          nsMenuUtilsX::gIsSynchronouslyActivatingNativeMenuItemDuringTest);
      nsMenuUtilsX::gIsSynchronouslyActivatingNativeMenuItemDuringTest = true;
      [parent performActionForItemAtIndex:[parent indexOfItem:item]];
      return NS_OK;
    }
  }
  return NS_ERROR_FAILURE;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

// Used for testing native menu system structure and event handling.
nsresult nsCocoaWindow::ForceUpdateNativeMenuAt(const nsAString& indexString) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;
  if (nsMenuBarX* mb = GetMenuBar()) {
    if (indexString.IsEmpty())
      mb->ForceNativeMenuReload();
    else
      mb->ForceUpdateNativeMenuAt(indexString);
  }
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

#pragma mark -

#ifdef INVALIDATE_DEBUGGING

static Boolean KeyDown(const UInt8 theKey) {
  KeyMap map;
  GetKeys(map);
  return ((*((UInt8*)map + (theKey >> 3)) >> (theKey & 7)) & 1) != 0;
}

static Boolean caps_lock() { return KeyDown(0x39); }

static void blinkRect(Rect* r) {
  StRegionFromPool oldClip;
  if (oldClip != NULL) ::GetClip(oldClip);

  ::ClipRect(r);
  ::InvertRect(r);
  UInt32 end = ::TickCount() + 5;
  while (::TickCount() < end);
  ::InvertRect(r);

  if (oldClip != NULL) ::SetClip(oldClip);
}

static void blinkRgn(RgnHandle rgn) {
  StRegionFromPool oldClip;
  if (oldClip != NULL) ::GetClip(oldClip);

  ::SetClip(rgn);
  ::InvertRgn(rgn);
  UInt32 end = ::TickCount() + 5;
  while (::TickCount() < end);
  ::InvertRgn(rgn);

  if (oldClip != NULL) ::SetClip(oldClip);
}

#endif

// Invalidate this component's visible area
void nsCocoaWindow::Invalidate(const LayoutDeviceIntRect& aRect) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mChildView || !mWindow.isVisibleOrBeingShown) {
    return;
  }

  NS_ASSERTION(
      GetWindowRenderer()->GetBackendType() != LayersBackend::LAYERS_WR,
      "Shouldn't need to invalidate with accelerated OMTC layers!");

  EnsureContentLayerForMainThreadPainting();
  mContentLayerInvalidRegion.OrWith(aRect.Intersect(
      LayoutDeviceIntRect(LayoutDeviceIntPoint(), GetClientBounds().Size())));
  [mChildView markLayerForDisplay];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

#pragma mark -

void nsCocoaWindow::PaintWindow() {
  if (nsIWidgetListener* listener = GetPaintListener()) {
    listener->PaintWindow(this);
  }
}

void nsCocoaWindow::PaintWindowInDrawTarget(
    gfx::DrawTarget* aDT, const LayoutDeviceIntRegion& aRegion,
    const gfx::IntSize& aSurfaceSize) {
  if (!aDT || !aDT->IsValid()) {
    return;
  }
  gfxContext targetContext(aDT);

  // Set up the clip region and clear existing contents in the backing surface.
  targetContext.NewPath();
  for (auto iter = aRegion.RectIter(); !iter.Done(); iter.Next()) {
    const LayoutDeviceIntRect& r = iter.Get();
    targetContext.Rectangle(gfxRect(r.x, r.y, r.width, r.height));
    aDT->ClearRect(gfx::Rect(r.ToUnknownRect()));
  }
  targetContext.Clip();

  nsAutoRetainCocoaObject kungFuDeathGrip(mChildView);
  if (GetWindowRenderer()->GetBackendType() == LayersBackend::LAYERS_NONE) {
    nsIWidget::AutoLayerManagerSetup setupLayerManager(this, &targetContext);
    PaintWindow();
  }
}

void nsCocoaWindow::EnsureContentLayerForMainThreadPainting() {
  // Ensure we have an mContentLayer of the correct size.
  // The content layer gets created on demand for BasicLayers windows. We do
  // not create it during widget creation because, for non-BasicLayers windows,
  // the compositing layer manager will create any layers it needs.
  auto size = GetClientBounds().Size();
  if (mContentLayer && mContentLayer->GetSize() != size.ToUnknownSize()) {
    mNativeLayerRoot->RemoveLayer(mContentLayer);
    mContentLayer = nullptr;
  }
  if (!mContentLayer) {
    mPoolHandle = SurfacePool::Create(0)->GetHandleForGL(nullptr);
    RefPtr<NativeLayer> contentLayer =
        mNativeLayerRoot->CreateLayer(size.ToUnknownSize(), false, mPoolHandle);
    mNativeLayerRoot->AppendLayer(contentLayer);
    mContentLayer = contentLayer->AsNativeLayerCA();
    mContentLayer->SetSurfaceIsFlipped(false);
    mContentLayerInvalidRegion =
        LayoutDeviceIntRect(LayoutDeviceIntPoint(), size);
  }
}

void nsCocoaWindow::PaintWindowInContentLayer() {
  if (GetClientBounds().IsEmpty()) {
    return;
  }
  EnsureContentLayerForMainThreadPainting();
  mPoolHandle->OnBeginFrame();
  RefPtr<DrawTarget> dt = mContentLayer->NextSurfaceAsDrawTarget(
      gfx::IntRect({}, mContentLayer->GetSize()),
      mContentLayerInvalidRegion.ToUnknownRegion(), gfx::BackendType::SKIA);
  if (!dt) {
    return;
  }

  PaintWindowInDrawTarget(dt, mContentLayerInvalidRegion, dt->GetSize());
  mContentLayer->NotifySurfaceReady();
  mContentLayerInvalidRegion.SetEmpty();
  mPoolHandle->OnEndFrame();
}

void nsCocoaWindow::HandleMainThreadCATransaction() {
  AUTO_PROFILER_MARKER("HandleMainThreadCATransaction", GRAPHICS);

  if (GetWindowRenderer()->GetBackendType() == LayersBackend::LAYERS_NONE) {
    // We're in BasicLayers mode, i.e. main thread software compositing.
    // Composite the window into our layer's surface.
    PaintWindowInContentLayer();
  } else {
    // Trigger a synchronous OMTC composite. This will call NextSurface and
    // NotifySurfaceReady on the compositor thread to update mNativeLayerRoot's
    // contents, and the main thread (this thread) will wait inside PaintWindow
    // during that time.
    PaintWindow();
  }

  {
    // Apply the changes inside mNativeLayerRoot to the underlying CALayers. Now
    // is a good time to call this because we know we're currently inside a main
    // thread CATransaction, and the lock makes sure that no composition is
    // currently in progress, so we won't present half-composited state to the
    // screen.
    MutexAutoLock lock(mCompositingLock);
    mNativeLayerRoot->CommitToScreen();
  }

  MaybeScheduleUnsuspendAsyncCATransactions();
}

void nsCocoaWindow::CreateCompositor(int aWidth, int aHeight) {
  // Ensure we are on the parent process.
  MOZ_ASSERT(XRE_IsParentProcess());

  // It's possible we might reach this before the GPU process has even
  // been started. That makes it hard to reason about the different
  // scenarios, which are:
  // 1) GPU process started successfully, and we're creating a compositor
  //    that should run on that process.
  // 2) GPU process startup failed, and we're creating an in-process
  //    compositor.
  // To clarify, we'll attempt to start the gpu process ourself, and
  // handle the error cases here.

  // Make sure the gfxPlatform is initialized, which is necessary to create
  // the GPUProcessManager.
  (void)gfxPlatform::GetPlatform();
  MOZ_ASSERT(
      mozilla::gfx::GPUProcessManager::Get(),
      "Getting the gfxPlatform should have created the GPUProcessManager.");

  // Do the rest of the compositor setup.
  nsIWidget::CreateCompositor(aWidth, aHeight);
}

void nsCocoaWindow::GetCompositorWidgetInitData(
    mozilla::widget::CompositorWidgetInitData* aInitData) {
  // If we already have an mNativeLayerRootRemoteMacParent, close it first.
  // This happens when we retry after the previous compositor session failed
  // to be created.
  if (RefPtr<NativeLayerRootRemoteMacParent> actor =
          std::move(mNativeLayerRootRemoteMacParent)) {
    MOZ_ASSERT(CompositorThread());
    CompositorThread()->Dispatch(
        NewRunnableMethod("NativeLayerRootRemoteMacParent::Close", actor,
                          &NativeLayerRootRemoteMacParent::Close));
  }

  // Create NativeLayerRemoteMac endpoints. The "parent" endpoint will
  // always connect to the parent process. The "child" endpoint will
  // connect to the gpu process, if it exists, otherwise the parent
  // process. Either way, the remaining code will bind the parent endpoint
  // on the parent process compositor thread.
  auto* pm = mozilla::gfx::GPUProcessManager::Get();
  mozilla::ipc::EndpointProcInfo gpuProcessInfo = pm->GPUEndpointProcInfo();

  mozilla::ipc::EndpointProcInfo childProcessInfo =
      gpuProcessInfo != mozilla::ipc::EndpointProcInfo::Invalid()
          ? gpuProcessInfo
          : mozilla::ipc::EndpointProcInfo::Current();

  mozilla::ipc::Endpoint<PNativeLayerRemoteParent> parentEndpoint;
  mozilla::ipc::Endpoint<PNativeLayerRemoteChild> childEndpoint;
  auto rv = PNativeLayerRemote::CreateEndpoints(
      mozilla::ipc::EndpointProcInfo::Current(), childProcessInfo,
      &parentEndpoint, &childEndpoint);
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  MOZ_RELEASE_ASSERT(parentEndpoint.IsValid());
  MOZ_RELEASE_ASSERT(childEndpoint.IsValid());

  // Create our mNativeLayerRootRemoteMacParent.
  RefPtr<NativeLayerRootRemoteMacParent> nativeLayerRemoteParent =
      new NativeLayerRootRemoteMacParent(mNativeLayerRoot);

  // Bind the parent endpoint compositor thread.
  MOZ_ASSERT(CompositorThread());

  Monitor monitor("nsCocoaWindow::GetCompositorWidgetInitData");
  bool didBindParentEndpoint = false;

  CompositorThread()->Dispatch(NS_NewRunnableFunction(
      "nsCocoaWindow::GetCompositorWidgetInitData bind endpoint", [&]() {
        // Bind the endpoint. This has to happen before the child endpoint is
        // sent to the GPU process.
        MOZ_ALWAYS_TRUE(parentEndpoint.Bind(nativeLayerRemoteParent));
        MonitorAutoLock lock(monitor);
        didBindParentEndpoint = true;
        lock.Notify();
      }));

  // Block until the parent endpoint is bound, so that the childEndpoint we
  // return in our CompositorWidgetInitData is ready to be sent to the GPU
  // process.
  {
    MonitorAutoLock lock(monitor);
    while (!didBindParentEndpoint) {
      lock.Wait();
    }
  }

  auto deviceIntRect = GetClientBounds();
  *aInitData = mozilla::widget::CocoaCompositorWidgetInitData(
      deviceIntRect.Size(), std::move(childEndpoint));

  mNativeLayerRootRemoteMacParent = std::move(nativeLayerRemoteParent);
}

void nsCocoaWindow::DestroyCompositor() {
  // The main work here is to close the mNativeLayerRootRemoteMacParent.
  // The call to Bind was done on the compositor thread, so we need to
  // dispatch to that thread. Move the existing
  // mNativeLayerRootRemoteMacParent out, leaving it null.
  if (RefPtr<NativeLayerRootRemoteMacParent> actor =
          std::move(mNativeLayerRootRemoteMacParent)) {
    MOZ_ASSERT(CompositorThread());
    CompositorThread()->Dispatch(
        NewRunnableMethod("NativeLayerRootRemoteMacParent::Close", actor,
                          &NativeLayerRootRemoteMacParent::Close));
  }

  nsIWidget::DestroyCompositor();
}

void nsCocoaWindow::NotifyCompositorSessionLost(
    mozilla::layers::CompositorSession* aSession) {
  // The GPU process has crashed.
  //
  // Trigger a repaint of this window.
  // Without this, WebRender won't receive a display list for this
  // window until something else triggers a parent-side repaint. So tab
  // contents would remain frozen until the user moves the mouse over a
  // UI element, for example.
  //
  // The Windows equivalent to this code is InvalidateWindowForDeviceReset.
  //
  // We wait for a short time before the repaint in order to avoid
  // flashing tab content - if we were to trigger the parent-side paint
  // immediately, the composite would briefly hide the tab contents because
  // the new compositor won't have a display list for the tab yet.
  const double kTriggerPaintDelayAfterGpuProcessCrash = 0.4;  // 400ms
  [mChildView performSelector:@selector(markLayerForDisplay)
                   withObject:nil
                   afterDelay:kTriggerPaintDelayAfterGpuProcessCrash];

  nsIWidget::NotifyCompositorSessionLost(aSession);
}

void nsCocoaWindow::SetCompositorWidgetDelegate(
    mozilla::widget::CompositorWidgetDelegate* aDelegate) {
  if (aDelegate) {
    mCompositorWidgetDelegate = aDelegate->AsPlatformSpecificDelegate();
    MOZ_ASSERT(mCompositorWidgetDelegate,
               "nsCocoaWindow::SetCompositorWidgetDelegate called with a "
               "non-PlatformCompositorWidgetDelegate");
  } else {
    mCompositorWidgetDelegate = nullptr;
  }
}

mozilla::layers::CompositorBridgeChild*
nsCocoaWindow::GetCompositorBridgeChild() const {
  return mCompositorBridgeChild;
}

/* static */
bool nsCocoaWindow::DoHasPendingInputEvent() {
  return sLastInputEventCount != GetCurrentInputEventCount();
}

/* static */
uint32_t nsCocoaWindow::GetCurrentInputEventCount() {
  // Can't use kCGAnyInputEventType because that updates too rarely for us (and
  // always in increments of 30+!) and because apparently it's sort of broken
  // on Tiger.  So just go ahead and query the counters we care about.
  static const CGEventType eventTypes[] = {kCGEventLeftMouseDown,
                                           kCGEventLeftMouseUp,
                                           kCGEventRightMouseDown,
                                           kCGEventRightMouseUp,
                                           kCGEventMouseMoved,
                                           kCGEventLeftMouseDragged,
                                           kCGEventRightMouseDragged,
                                           kCGEventKeyDown,
                                           kCGEventKeyUp,
                                           kCGEventScrollWheel,
                                           kCGEventTabletPointer,
                                           kCGEventOtherMouseDown,
                                           kCGEventOtherMouseUp,
                                           kCGEventOtherMouseDragged};

  uint32_t eventCount = 0;
  for (uint32_t i = 0; i < std::size(eventTypes); ++i) {
    eventCount += CGEventSourceCounterForEventType(
        kCGEventSourceStateCombinedSessionState, eventTypes[i]);
  }
  return eventCount;
}

/* static */
void nsCocoaWindow::UpdateCurrentInputEventCount() {
  sLastInputEventCount = GetCurrentInputEventCount();
}

#pragma mark -

void nsCocoaWindow::SetInputContext(const InputContext& aContext,
                                    const InputContextAction& aAction) {
  NS_ENSURE_TRUE_VOID(mTextInputHandler);

  if (mTextInputHandler->IsFocused()) {
    if (aContext.IsPasswordEditor()) {
      TextInputHandler::EnableSecureEventInput();
    } else {
      TextInputHandler::EnsureSecureEventInputDisabled();
    }
  }

  // IMEInputHandler::IsEditableContent() returns false when both
  // IsASCIICableOnly() and IsIMEEnabled() return false.  So, be careful
  // when you change the following code.  You might need to change
  // IMEInputHandler::IsEditableContent() too.
  mInputContext = aContext;

  switch (aContext.mIMEState.mEnabled) {
    case IMEEnabled::Enabled:
      mTextInputHandler->SetASCIICapableOnly(false);
      mTextInputHandler->EnableIME(true);
      if (mInputContext.mIMEState.mOpen != IMEState::DONT_CHANGE_OPEN_STATE) {
        mTextInputHandler->SetIMEOpenState(mInputContext.mIMEState.mOpen ==
                                           IMEState::OPEN);
      }
      mTextInputHandler->EnableTextSubstitution(aContext.mAutocorrect);
      break;
    case IMEEnabled::Disabled:
      mTextInputHandler->SetASCIICapableOnly(false);
      mTextInputHandler->EnableIME(false);
      mTextInputHandler->EnableTextSubstitution(false);
      break;
    case IMEEnabled::Password:
      mTextInputHandler->SetASCIICapableOnly(true);
      mTextInputHandler->EnableIME(false);
      mTextInputHandler->EnableTextSubstitution(aContext.mAutocorrect);
      break;
    default:
      NS_ERROR("not implemented!");
  }
}

InputContext nsCocoaWindow::GetInputContext() {
  switch (mInputContext.mIMEState.mEnabled) {
    case IMEEnabled::Enabled:
      if (mTextInputHandler) {
        mInputContext.mIMEState.mOpen = mTextInputHandler->IsIMEOpened()
                                            ? IMEState::OPEN
                                            : IMEState::CLOSED;
        break;
      }
      // If mTextInputHandler is null, set CLOSED instead...
      [[fallthrough]];
    default:
      mInputContext.mIMEState.mOpen = IMEState::CLOSED;
      break;
  }
  return mInputContext;
}

TextEventDispatcherListener*
nsCocoaWindow::GetNativeTextEventDispatcherListener() {
  if (NS_WARN_IF(!mTextInputHandler)) {
    return nullptr;
  }
  return mTextInputHandler;
}

nsresult nsCocoaWindow::AttachNativeKeyEvent(
    mozilla::WidgetKeyboardEvent& aEvent) {
  NS_ENSURE_TRUE(mTextInputHandler, NS_ERROR_NOT_AVAILABLE);
  return mTextInputHandler->AttachNativeKeyEvent(aEvent);
}

NSView<mozView>* nsCocoaWindow::GetEditorView() {
  NSView<mozView>* editorView = mChildView;
  // We need to get editor's view. E.g., when the focus is in the bookmark
  // dialog, the view is <panel> element of the dialog.  At this time, the key
  // events are processed the parent window's view that has native focus.
  WidgetQueryContentEvent queryContentState(true, eQueryContentState, this);
  // This may be called during creating a menu popup frame due to creating
  // widget synchronously and that causes Cocoa asking current window level.
  // In this case, it's not safe to flush layout on the document and we don't
  // need any layout information right now.
  queryContentState.mNeedsToFlushLayout = false;
  DispatchWindowEvent(queryContentState);
  if (queryContentState.Succeeded() &&
      queryContentState.mReply->mFocusedWidget) {
    NSView<mozView>* view = static_cast<NSView<mozView>*>(
        queryContentState.mReply->mFocusedWidget->GetNativeData(
            NS_NATIVE_WIDGET));
    if (view) editorView = view;
  }
  return editorView;
}

#pragma mark -

bool nsCocoaWindow::PreRender(WidgetRenderingContext* aContext)
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // The lock makes sure that we don't attempt to tear down the view while
  // compositing. That would make us unable to call postRender on it when the
  // composition is done, thus keeping the GL context locked forever.
  mCompositingLock.Lock();

  if (aContext->mGL && StaticPrefs::gfx_compositor_gpu_migration()) {
    GLContextCGL::Cast(aContext->mGL)->MigrateToActiveGPU();
  }

  return true;
}

void nsCocoaWindow::PostRender(WidgetRenderingContext* aContext)
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mCompositingLock.Unlock();
}

layers::NativeLayerRoot* nsCocoaWindow::GetNativeLayerRoot() {
  return mNativeLayerRoot;
}

static LayoutDeviceIntRect FindFirstRectOfType(
    const nsTArray<nsIWidget::ThemeGeometry>& aThemeGeometries,
    nsITheme::ThemeGeometryType aThemeGeometryType) {
  for (uint32_t i = 0; i < aThemeGeometries.Length(); ++i) {
    const nsIWidget::ThemeGeometry& g = aThemeGeometries[i];
    if (g.mType == aThemeGeometryType) {
      return g.mRect;
    }
  }
  return LayoutDeviceIntRect();
}

void nsCocoaWindow::UpdateThemeGeometries(
    const nsTArray<ThemeGeometry>& aThemeGeometries) {
  if (!mChildView.window) {
    return;
  }

  UpdateVibrancy(aThemeGeometries);

  if (![mChildView.window isKindOfClass:[ToolbarWindow class]]) {
    return;
  }

  ToolbarWindow* win = (ToolbarWindow*)[mChildView window];

  // Update titlebar control offsets.
  LayoutDeviceIntRect windowButtonRect =
      FindFirstRectOfType(aThemeGeometries, eThemeGeometryTypeWindowButtons);
  [win placeWindowButtons:[mChildView convertRect:DevPixelsToCocoaPoints(
                                                      windowButtonRect)
                                           toView:nil]];
}

static Maybe<VibrancyType> ThemeGeometryTypeToVibrancyType(
    nsITheme::ThemeGeometryType aThemeGeometryType) {
  switch (aThemeGeometryType) {
    case eThemeGeometryTypeSidebar:
      return Some(VibrancyType::Sidebar);
    case eThemeGeometryTypeTitlebar:
      return Some(VibrancyType::Titlebar);
    default:
      return Nothing();
  }
}

static EnumeratedArray<VibrancyType, LayoutDeviceIntRegion>
GatherVibrantRegions(Span<const nsIWidget::ThemeGeometry> aThemeGeometries) {
  EnumeratedArray<VibrancyType, LayoutDeviceIntRegion> regions;
  for (const auto& geometry : aThemeGeometries) {
    auto vibrancyType = ThemeGeometryTypeToVibrancyType(geometry.mType);
    if (!vibrancyType) {
      continue;
    }
    regions[*vibrancyType].OrWith(geometry.mRect);
  }
  return regions;
}

// Subtracts parts from regions in such a way that they don't have any overlap.
// Each region in the argument list will have the union of all the regions
// *following* it subtracted from itself. In other words, the arguments are
// treated as low priority to high priority.
static void MakeRegionsNonOverlapping(Span<LayoutDeviceIntRegion> aRegions) {
  LayoutDeviceIntRegion unionOfAll;
  for (auto& region : aRegions) {
    region.SubOut(unionOfAll);
    unionOfAll.OrWith(region);
  }
}

void nsCocoaWindow::UpdateVibrancy(
    const nsTArray<ThemeGeometry>& aThemeGeometries) {
  auto regions = GatherVibrantRegions(aThemeGeometries);
  MakeRegionsNonOverlapping(regions);

  auto& vm = EnsureVibrancyManager();
  bool changed = false;

  // EnumeratedArray doesn't have an iterator that also yields the enum type,
  // but we rely on VibrancyType being contiguous and starting at 0, so we can
  // do that manually.
  size_t i = 0;
  for (const auto& region : regions) {
    changed |= vm.UpdateVibrantRegion(VibrancyType(i++), region);
  }

  if (changed) {
    SuspendAsyncCATransactions();
  }
}

mozilla::VibrancyManager& nsCocoaWindow::EnsureVibrancyManager() {
  MOZ_ASSERT(mChildView, "Only call this once we have a view!");
  if (!mVibrancyManager) {
    mVibrancyManager =
        MakeUnique<VibrancyManager>(*this, mChildView.vibrancyViewsContainer);
  }
  return *mVibrancyManager;
}

@interface NonDraggableView : NSView
@end

@implementation NonDraggableView
- (BOOL)mouseDownCanMoveWindow {
  return NO;
}
- (NSView*)hitTest:(NSPoint)aPoint {
  return nil;
}
- (NSRect)_opaqueRectForWindowMoveWhenInTitlebar {
  // In NSWindows that use NSWindowStyleMaskFullSizeContentView, NSViews which
  // overlap the titlebar do not disable window dragging in the overlapping
  // areas even if they return NO from mouseDownCanMoveWindow. This can have
  // unfortunate effects: For example, dragging tabs in a browser window would
  // move the window if those tabs are in the titlebar.
  // macOS does not seem to offer a documented way to opt-out of the forced
  // window dragging in the titlebar.
  // Overriding _opaqueRectForWindowMoveWhenInTitlebar is an undocumented way
  // of opting out of this behavior. This method was added in 10.11 and is used
  // by some NSControl subclasses to prevent window dragging in the titlebar.
  // The function which assembles the draggable area of the window calls
  // _opaqueRect for the content area and _opaqueRectForWindowMoveWhenInTitlebar
  // for the titlebar area, on all visible NSViews. The default implementation
  // of _opaqueRect returns [self visibleRect], and the default implementation
  // of _opaqueRectForWindowMoveWhenInTitlebar returns NSZeroRect unless it's
  // overridden.
  //
  // Since this view is constructed and used such that its entire bounds is the
  // relevant region, we just return our bounds.
  return self.bounds;
}
@end

void nsCocoaWindow::UpdateWindowDraggingRegion(
    const LayoutDeviceIntRegion& aRegion) {
  // mChildView returns YES from mouseDownCanMoveWindow, so we need to put
  // NSViews that return NO from mouseDownCanMoveWindow in the places that
  // shouldn't be draggable. We can't do it the other way round because
  // returning YES from mouseDownCanMoveWindow doesn't have any effect if
  // there's a superview that returns NO.
  LayoutDeviceIntRegion nonDraggable;
  nonDraggable.Sub(LayoutDeviceIntRect(0, 0, mBounds.width, mBounds.height),
                   aRegion);

  __block bool changed = false;

  // Suppress calls to setNeedsDisplay during NSView geometry changes.
  ManipulateViewWithoutNeedingDisplay(mChildView, ^() {
    changed = mNonDraggableRegion.UpdateRegion(
        nonDraggable, *this, mChildView.nonDraggableViewsContainer, ^() {
          return [[NonDraggableView alloc] initWithFrame:NSZeroRect];
        });
  });

  if (changed) {
    // Trigger an update to the window server. This will call
    // mouseDownCanMoveWindow.
    // Doing this manually is only necessary because we're suppressing
    // setNeedsDisplay calls above.
    [[mChildView window] setMovableByWindowBackground:NO];
    [[mChildView window] setMovableByWindowBackground:YES];
  }
}

nsEventStatus nsCocoaWindow::DispatchAPZInputEvent(InputData& aEvent) {
  APZEventResult result;

  if (mAPZC) {
    result = mAPZC->InputBridge()->ReceiveInputEvent(aEvent);
  }

  if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
    return result.GetStatus();
  }

  if (aEvent.mInputType == PINCHGESTURE_INPUT) {
    PinchGestureInput& pinchEvent = aEvent.AsPinchGestureInput();
    WidgetWheelEvent wheelEvent = pinchEvent.ToWidgetEvent(this);
    ProcessUntransformedAPZEvent(&wheelEvent, result);
  } else if (aEvent.mInputType == TAPGESTURE_INPUT) {
    TapGestureInput& tapEvent = aEvent.AsTapGestureInput();
    WidgetSimpleGestureEvent gestureEvent = tapEvent.ToWidgetEvent(this);
    ProcessUntransformedAPZEvent(&gestureEvent, result);
  } else {
    MOZ_ASSERT_UNREACHABLE();
  }

  return result.GetStatus();
}

void nsCocoaWindow::DispatchAPZWheelInputEvent(InputData& aEvent) {
  if (mSwipeTracker && aEvent.mInputType == PANGESTURE_INPUT) {
    // Give the swipe tracker a first pass at the event. If a new pan gesture
    // has been started since the beginning of the swipe, the swipe tracker
    // will know to ignore the event.
    nsEventStatus status =
        mSwipeTracker->ProcessEvent(aEvent.AsPanGestureInput());
    if (status == nsEventStatus_eConsumeNoDefault) {
      return;
    }
  }

  WidgetWheelEvent event(true, eWheel, this);

  if (mAPZC) {
    APZEventResult result;

    switch (aEvent.mInputType) {
      case PANGESTURE_INPUT: {
        result = mAPZC->InputBridge()->ReceiveInputEvent(aEvent);
        if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
          return;
        }

        event = MayStartSwipeForAPZ(aEvent.AsPanGestureInput(), result);
        break;
      }
      case SCROLLWHEEL_INPUT: {
        // For wheel events on macOS, send it to APZ using the WidgetInputEvent
        // variant of ReceiveInputEvent, because the APZInputBridge version of
        // that function has special handling (for delta multipliers etc.) that
        // we need to run. Using the InputData variant would bypass that and
        // go straight to the APZCTreeManager subclass.
        event = aEvent.AsScrollWheelInput().ToWidgetEvent(this);
        result = mAPZC->InputBridge()->ReceiveInputEvent(event);
        if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
          return;
        }
        break;
      };
      default:
        MOZ_CRASH("unsupported event type");
        return;
    }
    if (event.mMessage == eWheel &&
        (event.mDeltaX != 0 || event.mDeltaY != 0)) {
      ProcessUntransformedAPZEvent(&event, result);
    }
    return;
  }

  switch (aEvent.mInputType) {
    case PANGESTURE_INPUT: {
      if (MayStartSwipeForNonAPZ(aEvent.AsPanGestureInput())) {
        return;
      }
      event = aEvent.AsPanGestureInput().ToWidgetEvent(this);
      break;
    }
    case SCROLLWHEEL_INPUT: {
      event = aEvent.AsScrollWheelInput().ToWidgetEvent(this);
      break;
    }
    default:
      MOZ_CRASH("unexpected event type");
      return;
  }
  if (event.mMessage == eWheel && (event.mDeltaX != 0 || event.mDeltaY != 0)) {
    DispatchEvent(&event);
  }
}

void nsCocoaWindow::DispatchDoubleTapGesture(
    TimeStamp aEventTimeStamp, LayoutDeviceIntPoint aScreenPosition,
    mozilla::Modifiers aModifiers) {
  if (StaticPrefs::apz_mac_enable_double_tap_zoom_touchpad_gesture()) {
    TapGestureInput event{
        TapGestureInput::TAPGESTURE_DOUBLE, aEventTimeStamp,
        ViewAs<ScreenPixel>(
            aScreenPosition,
            PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent),
        aModifiers};

    DispatchAPZInputEvent(event);
  } else {
    // Setup the "double tap" event.
    WidgetSimpleGestureEvent geckoEvent(true, eTapGesture, this);
    // do what convertCocoaMouseEvent does basically.
    geckoEvent.mRefPoint = aScreenPosition;
    geckoEvent.mModifiers = aModifiers;
    geckoEvent.mTimeStamp = aEventTimeStamp;
    geckoEvent.mClickCount = 1;

    // Send the event.
    DispatchWindowEvent(geckoEvent);
  }
}

void nsCocoaWindow::LookUpDictionary(
    const nsAString& aText, const nsTArray<mozilla::FontRange>& aFontRangeArray,
    const bool aIsVertical, const LayoutDeviceIntPoint& aPoint) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  NSMutableAttributedString* attrStr =
      nsCocoaUtils::GetNSMutableAttributedString(
          aText, aFontRangeArray, aIsVertical, BackingScaleFactor());
  NSPoint pt =
      nsCocoaUtils::DevPixelsToCocoaPoints(aPoint, BackingScaleFactor());
  NSDictionary* attributes = [attrStr attributesAtIndex:0 effectiveRange:nil];
  NSFont* font = [attributes objectForKey:NSFontAttributeName];
  if (font) {
    if (aIsVertical) {
      pt.x -= [font descender];
    } else {
      pt.y += [font ascender];
    }
  }

  [mChildView showDefinitionForAttributedString:attrStr atPoint:pt];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

#ifdef ACCESSIBILITY
already_AddRefed<a11y::LocalAccessible> nsCocoaWindow::GetWindowAccessible() {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) return nullptr;

  if (GetWindowType() == WindowType::Popup &&
      GetPopupType() != PopupType::Panel) {
    // If we are a non-panel popup, like a menu or tooltip, we want to return
    // null. We rely on the gecko tree hierarchy instead of the native widget
    // one to expose this accessible.
    return nullptr;
  }

  // mAccessible might be dead if accessibility was previously disabled and is
  // now being enabled again.
  if (mAccessible && mAccessible->IsAlive()) {
    RefPtr<a11y::LocalAccessible> ret;
    CallQueryReferent(mAccessible.get(), static_cast<a11y::LocalAccessible**>(
                                             getter_AddRefs(ret)));
    return ret.forget();
  }

  // need to fetch the accessible anew, because it has gone away.
  // cache the accessible in our weak ptr
  RefPtr<a11y::LocalAccessible> acc = GetRootAccessible();
  if (GetWindowType() == WindowType::Popup) {
    // If we're a popup panel, we want to return the accessible for the
    // content of the panel, not the accessible for the document.
    if (nsIFrame* popupFrame = GetFrame()) {
      acc = acc->AsDoc()->GetAccessible(popupFrame->GetContent());
    }
  }

  mAccessible = do_GetWeakReference(acc.get());

  return acc.forget();
}
#endif

class WidgetsReleaserRunnable final : public mozilla::Runnable {
 public:
  explicit WidgetsReleaserRunnable(nsTArray<nsCOMPtr<nsIWidget>>&& aWidgetArray)
      : mozilla::Runnable("WidgetsReleaserRunnable"),
        mWidgetArray(std::move(aWidgetArray)) {}

  // Do nothing; all this runnable does is hold a reference to the widgets in
  // mWidgetArray, and those references will be dropped when this runnable
  // is destroyed.

 private:
  nsTArray<nsCOMPtr<nsIWidget>> mWidgetArray;
};

#pragma mark -

// ViewRegionContainerView is a view class for certain subviews of ChildView
// which contain the NSViews created for ViewRegions (see ViewRegion.h).
// It doesn't do anything interesting, it only acts as a container so that it's
// easier for ChildView to control the z order of its children.
@interface ViewRegionContainerView : NSView {
}
@end

@implementation ViewRegionContainerView

- (NSView*)hitTest:(NSPoint)aPoint {
  return nil;  // Be transparent to mouse events.
}

- (BOOL)isFlipped {
  return [[self superview] isFlipped];
}

- (BOOL)mouseDownCanMoveWindow {
  return [[self superview] mouseDownCanMoveWindow];
}

@end

@implementation ChildView

// globalDragPboard is non-null during native drag sessions that did not
// originate in our native NSView (it is set in |draggingEntered:|). It is unset
// when the drag session ends for this view, either with the mouse exiting or
// when a drop occurs in this view.
NSPasteboard* globalDragPboard = nil;

// gLastDragView and gLastDragMouseDownEvent are used to communicate information
// to the drag service during drag invocation (starting a drag in from the
// view). gLastDragView is only non-null while a mouse button is pressed, so
// between mouseDown and mouseUp.
NSView* gLastDragView = nil;             // [weak]
NSEvent* gLastDragMouseDownEvent = nil;  // [strong]

+ (void)initialize {
  static BOOL initialized = NO;

  if (!initialized) {
    // Inform the OS about the types of services (from the "Services" menu)
    // that we can handle.
    NSArray* types = @[
      [UTIHelper stringFromPboardType:NSPasteboardTypeString],
      [UTIHelper stringFromPboardType:NSPasteboardTypeHTML]
    ];
    [NSApp registerServicesMenuSendTypes:types returnTypes:types];
    initialized = YES;
  }
}

+ (void)registerViewForDraggedTypes:(NSView*)aView {
  [aView
      registerForDraggedTypes:
          [NSArray
              arrayWithObjects:
                  [UTIHelper stringFromPboardType:NSFilenamesPboardType],
                  [UTIHelper stringFromPboardType:kMozFileUrlsPboardType],
                  [UTIHelper stringFromPboardType:NSPasteboardTypeString],
                  [UTIHelper stringFromPboardType:NSPasteboardTypeHTML],
                  [UTIHelper
                      stringFromPboardType:(NSString*)
                                               kPasteboardTypeFileURLPromise],
                  [UTIHelper stringFromPboardType:kMozWildcardPboardType],
                  [UTIHelper stringFromPboardType:kPublicUrlPboardType],
                  [UTIHelper stringFromPboardType:kPublicUrlNamePboardType],
                  [UTIHelper stringFromPboardType:kUrlsWithTitlesPboardType],
                  nil]];
}

// initWithFrame:geckoChild:
- (id)initWithFrame:(NSRect)inFrame geckoChild:(nsCocoaWindow*)inChild {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  self = [super initWithFrame:inFrame];
  if (!self) {
    return nil;
  }

  mGeckoChild = inChild;
  mBlockedLastMouseDown = NO;
  mExpectingWheelStop = NO;

  mLastMouseDownEvent = nil;
  mLastKeyDownEvent = nil;
  mClickThroughMouseDownEvent = nil;
  mDragService = nullptr;

  mGestureState = eGestureState_None;
  mCumulativeRotation = 0.0;

  mIsUpdatingLayer = NO;

  [self setFocusRingType:NSFocusRingTypeNone];

#ifdef __LP64__
  mCancelSwipeAnimation = nil;
#endif

  auto bounds = self.bounds;
  mNonDraggableViewsContainer =
      [[ViewRegionContainerView alloc] initWithFrame:bounds];
  mVibrancyViewsContainer =
      [[ViewRegionContainerView alloc] initWithFrame:bounds];

  mNonDraggableViewsContainer.autoresizingMask =
      mVibrancyViewsContainer.autoresizingMask =
          NSViewWidthSizable | NSViewHeightSizable;

  [self addSubview:mNonDraggableViewsContainer];
  [self addSubview:mVibrancyViewsContainer];

  mPixelHostingView = [[PixelHostingView alloc] initWithFrame:bounds];
  mPixelHostingView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  [self addSubview:mPixelHostingView];

  mRootCALayer = [[CALayer layer] retain];
  mRootCALayer.position = NSZeroPoint;
  mRootCALayer.bounds = NSZeroRect;
  mRootCALayer.anchorPoint = NSZeroPoint;
  mRootCALayer.contentsGravity = kCAGravityTopLeft;
  [mPixelHostingView.layer addSublayer:mRootCALayer];

  mLastPressureStage = 0;

  // register for things we'll take from other applications
  [ChildView registerViewForDraggedTypes:self];

  return self;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (NSTextInputContext*)inputContext {
  if (!mGeckoChild) {
    // -[ChildView widgetDestroyed] has been called, but
    // -[ChildView delayedTearDown] has not yet completed.  Accessing
    // [super inputContext] now would uselessly recreate a text input context
    // for us, under which -[ChildView validAttributesForMarkedText] would
    // be called and the assertion checking for mTextInputHandler would fail.
    // We return nil to avoid that.
    return nil;
  }
  return [super inputContext];
}

- (void)installTextInputHandler:(TextInputHandler*)aHandler {
  mTextInputHandler = aHandler;
}

- (void)uninstallTextInputHandler {
  mTextInputHandler = nullptr;
}

- (NSView*)vibrancyViewsContainer {
  return mVibrancyViewsContainer;
}

- (NSView*)nonDraggableViewsContainer {
  return mNonDraggableViewsContainer;
}

- (NSView*)pixelHostingView {
  return mPixelHostingView;
}

- (void)dealloc {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  [mLastMouseDownEvent release];
  [mLastKeyDownEvent release];
  [mClickThroughMouseDownEvent release];
  ChildViewMouseTracker::OnDestroyView(self);

  [mVibrancyViewsContainer removeFromSuperview];
  [mVibrancyViewsContainer release];
  [mNonDraggableViewsContainer removeFromSuperview];
  [mNonDraggableViewsContainer release];
  [mPixelHostingView removeFromSuperview];
  [mPixelHostingView release];
  [mRootCALayer release];

  if (gLastDragView == self) {
    gLastDragView = nil;
  }

  [super dealloc];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)widgetDestroyed {
  if (mTextInputHandler) {
    mTextInputHandler->OnDestroyWidget(mGeckoChild);
    mTextInputHandler = nullptr;
  }
  mGeckoChild = nullptr;

  // Just in case we're destroyed abruptly and missed the draggingExited
  // or performDragOperation message.
  NS_IF_RELEASE(mDragService);
}

// mozView method, return our gecko child view widget. Note this does not
// AddRef.
- (nsIWidget*)widget {
  return static_cast<nsIWidget*>(mGeckoChild);
}

- (NSString*)description {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  return [NSString stringWithFormat:@"ChildView %p, gecko child %p, frame %@",
                                    self, mGeckoChild,
                                    NSStringFromRect([self frame])];

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

// Make the origin of this view the topLeft corner (gecko origin) rather
// than the bottomLeft corner (standard cocoa origin).
- (BOOL)isFlipped {
  return YES;
}

// We accept key and mouse events, so don't keep passing them up the chain.
// Allow this to be a 'focused' widget for event dispatch.
- (BOOL)acceptsFirstResponder {
  return YES;
}

// Accept mouse down events on background windows
- (BOOL)acceptsFirstMouse:(NSEvent*)aEvent {
  if (![[self window] isKindOfClass:[PopupWindow class]]) {
    // We rely on this function to tell us that the mousedown was on a
    // background window. Inside mouseDown we can't tell whether we were
    // inactive because at that point we've already been made active.
    // Unfortunately, acceptsFirstMouse is called for PopupWindows even when
    // their parent window is active, so ignore this on them for now.
    [mClickThroughMouseDownEvent release];
    mClickThroughMouseDownEvent = [aEvent retain];
  }
  return YES;
}

- (BOOL)mouseDownCanMoveWindow {
  // Return YES so that parts of this view can be draggable. The non-draggable
  // parts will be covered by NSViews that return NO from
  // mouseDownCanMoveWindow and thus override draggability from the inside.
  // These views are assembled in nsCocoaWindow::UpdateWindowDraggingRegion.
  return YES;
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  if (mGeckoChild) {
    // actually, it could be the color space that's changed,
    // but we can't tell the difference here except by retrieving
    // the backing scale factor and comparing to the old value
    mGeckoChild->BackingScaleFactorChanged();
  }
}

- (void)showContextMenuForSelection:(id)sender {
  if (!mGeckoChild) {
    return;
  }
  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  WidgetPointerEvent geckoEvent(true, eContextMenu, mGeckoChild,
                                WidgetMouseEvent::eContextMenuKey);
  geckoEvent.mTimeStamp =
      nsCocoaUtils::GetEventTimeStamp([[NSApp currentEvent] timestamp]);
  geckoEvent.mRefPoint = {};
  mGeckoChild->DispatchInputEvent(&geckoEvent);
}

- (void)viewWillStartLiveResize {
  nsCocoaWindow* windowWidget = mGeckoChild;
  if (windowWidget) {
    windowWidget->NotifyLiveResizeStarted();
  }
}

- (void)viewDidEndLiveResize {
  // mGeckoChild may legitimately be null here. It should also have been null
  // in viewWillStartLiveResize, so there's no problem. However if we run into
  // cases where the windowWidget was non-null in viewWillStartLiveResize but
  // is null here, that might be problematic because we might get stuck with
  // a content process that has the displayport suppressed. If that scenario
  // arises (I'm not sure that it does) we will need to handle it gracefully.
  nsCocoaWindow* windowWidget = mGeckoChild;
  if (windowWidget) {
    windowWidget->NotifyLiveResizeStopped();
  }
}

- (void)markLayerForDisplay {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (!mIsUpdatingLayer) {
    // This call will cause updateRootCALayer to be called during the upcoming
    // main thread CoreAnimation transaction. It will also trigger a transaction
    // if no transaction is currently pending.
    [mPixelHostingView.layer setNeedsDisplay];
  }
}

- (void)ensureNextCompositeIsAtomicWithMainThreadPaint {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (mGeckoChild) {
    mGeckoChild->SuspendAsyncCATransactions();
  }
}

- (void)updateRootCALayer {
  if (NS_IsMainThread() && mGeckoChild) {
    MOZ_RELEASE_ASSERT(!mIsUpdatingLayer, "Re-entrant layer display?");
    mIsUpdatingLayer = YES;
    mGeckoChild->HandleMainThreadCATransaction();
    mIsUpdatingLayer = NO;
  }
}

- (CALayer*)rootCALayer {
  return mRootCALayer;
}

// If we've just created a non-native context menu, we need to mark it as
// such and let the OS (and other programs) know when it opens and closes
// (this is how the OS knows to close other programs' context menus when
// ours open).  We send the initial notification here, but others are sent
// in nsCocoaWindow::Show().
- (void)maybeInitContextMenuTracking {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mozilla::widget::NativeMenuSupport::ShouldUseNativeContextMenus()) {
    return;
  }

  nsIRollupListener* rollupListener = nsIWidget::GetActiveRollupListener();
  NS_ENSURE_TRUE_VOID(rollupListener);
  nsCOMPtr<nsIWidget> widget = rollupListener->GetRollupWidget();
  NS_ENSURE_TRUE_VOID(widget);

  NSWindow* popupWindow = (NSWindow*)widget->GetNativeData(NS_NATIVE_WINDOW);
  if (!popupWindow || ![popupWindow isKindOfClass:[PopupWindow class]]) return;

  [[NSDistributedNotificationCenter defaultCenter]
      postNotificationName:@"com.apple.HIToolbox.beginMenuTrackingNotification"
                    object:@"org.mozilla.gecko.PopupWindow"];
  [(PopupWindow*)popupWindow setIsContextMenu:YES];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Returns true if the event should no longer be processed, false otherwise.
// This does not return whether or not anything was rolled up.
- (BOOL)maybeRollup:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  BOOL consumeEvent = NO;

  nsIRollupListener* rollupListener = nsIWidget::GetActiveRollupListener();
  NS_ENSURE_TRUE(rollupListener, false);

  BOOL isWheelTypeEvent = [theEvent type] == NSEventTypeScrollWheel ||
                          [theEvent type] == NSEventTypeMagnify ||
                          [theEvent type] == NSEventTypeSmartMagnify;

  if (!isWheelTypeEvent && rollupListener->RollupNativeMenu()) {
    // A native menu was rolled up.
    // Don't consume this event; if the menu wanted to consume this event it
    // would already have done so and we wouldn't even get here. For example, we
    // won't get here for left clicks that close native menus (because the
    // native menu consumes it), but we will get here for right clicks that
    // close native menus, and we do not want to consume those right clicks.
    return NO;
  }

  nsCOMPtr<nsIWidget> rollupWidget = rollupListener->GetRollupWidget();
  if (!rollupWidget) {
    return consumeEvent;
  }

  NSWindow* currentPopup =
      static_cast<NSWindow*>(rollupWidget->GetNativeData(NS_NATIVE_WINDOW));
  if (nsCocoaUtils::IsEventOverWindow(theEvent, currentPopup)) {
    return consumeEvent;
  }

  // Check to see if scroll/zoom events should roll up the popup
  if (isWheelTypeEvent) {
    // consume scroll events that aren't over the popup unless the popup is an
    // arrow panel.
    consumeEvent = rollupListener->ShouldConsumeOnMouseWheelEvent();
    if (!rollupListener->ShouldRollupOnMouseWheelEvent()) {
      return consumeEvent;
    }
  }

  // if we're dealing with menus, we probably have submenus and
  // we don't want to rollup if the click is in a parent menu of
  // the current submenu
  uint32_t popupsToRollup = UINT32_MAX;
  AutoTArray<nsIWidget*, 5> widgetChain;
  uint32_t sameTypeCount = rollupListener->GetSubmenuWidgetChain(&widgetChain);
  for (uint32_t i = 0; i < widgetChain.Length(); i++) {
    nsIWidget* widget = widgetChain[i];
    NSWindow* currWindow = (NSWindow*)widget->GetNativeData(NS_NATIVE_WINDOW);
    if (nsCocoaUtils::IsEventOverWindow(theEvent, currWindow)) {
      // don't roll up if the mouse event occurred within a menu of the
      // same type. If the mouse event occurred in a menu higher than
      // that, roll up, but pass the number of popups to Rollup so
      // that only those of the same type close up.
      if (i < sameTypeCount) {
        return consumeEvent;
      }
      popupsToRollup = sameTypeCount;
      break;
    }
  }

  LayoutDeviceIntPoint devPoint;
  nsIRollupListener::RollupOptions rollupOptions{popupsToRollup};
  if ([theEvent type] == NSEventTypeLeftMouseDown) {
    NSPoint point = [NSEvent mouseLocation];
    FlipCocoaScreenCoordinate(point);
    devPoint = mGeckoChild->CocoaPointsToDevPixels(point);
    rollupOptions.mPoint = &devPoint;
  }
  consumeEvent = (BOOL)rollupListener->Rollup(rollupOptions);
  return consumeEvent;

  NS_OBJC_END_TRY_BLOCK_RETURN(NO);
}

- (void)swipeWithEvent:(NSEvent*)anEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!anEvent || !mGeckoChild) {
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  float deltaX = [anEvent deltaX];  // left=1.0, right=-1.0
  float deltaY = [anEvent deltaY];  // up=1.0, down=-1.0

  // Setup the "swipe" event.
  WidgetSimpleGestureEvent geckoEvent(true, eSwipeGesture, mGeckoChild);
  [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];

  // Record the left/right direction.
  if (deltaX > 0.0)
    geckoEvent.mDirection |= dom::SimpleGestureEvent_Binding::DIRECTION_LEFT;
  else if (deltaX < 0.0)
    geckoEvent.mDirection |= dom::SimpleGestureEvent_Binding::DIRECTION_RIGHT;

  // Record the up/down direction.
  if (deltaY > 0.0)
    geckoEvent.mDirection |= dom::SimpleGestureEvent_Binding::DIRECTION_UP;
  else if (deltaY < 0.0)
    geckoEvent.mDirection |= dom::SimpleGestureEvent_Binding::DIRECTION_DOWN;

  // Send the event.
  mGeckoChild->DispatchWindowEvent(geckoEvent);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Pinch zoom gesture.
- (void)magnifyWithEvent:(NSEvent*)anEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if ([self maybeRollup:anEvent]) {
    return;
  }

  if (!mGeckoChild) {
    return;
  }

  // Instead of calling beginOrEndGestureForEventPhase we basically inline
  // the effects of it here, because that function doesn't play too well with
  // how we create PinchGestureInput events below. The main point of that
  // function is to avoid flip-flopping between rotation/magnify gestures, which
  // we can do by checking and setting mGestureState appropriately. A secondary
  // result of that function is to send the final eMagnifyGesture event when
  // the gesture ends, but APZ takes care of that for us.
  if (mGestureState == eGestureState_RotateGesture &&
      [anEvent phase] != NSEventPhaseBegan) {
    // If we're already in a rotation and not "starting" a magnify, abort.
    return;
  }
  mGestureState = eGestureState_MagnifyGesture;

  NSPoint locationInWindow =
      nsCocoaUtils::EventLocationForWindow(anEvent, [self window]);
  ScreenPoint position = ViewAs<ScreenPixel>(
      [self convertWindowCoordinatesRoundDown:locationInWindow],
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);
  ExternalPoint screenOffset = ViewAs<ExternalPixel>(
      mGeckoChild->WidgetToScreenOffset(),
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);

  TimeStamp eventTimeStamp =
      nsCocoaUtils::GetEventTimeStamp([anEvent timestamp]);
  NSEventPhase eventPhase = [anEvent phase];
  PinchGestureInput::PinchGestureType pinchGestureType;

  switch (eventPhase) {
    case NSEventPhaseBegan: {
      pinchGestureType = PinchGestureInput::PINCHGESTURE_START;
      break;
    }
    case NSEventPhaseChanged: {
      pinchGestureType = PinchGestureInput::PINCHGESTURE_SCALE;
      break;
    }
    case NSEventPhaseEnded: {
      pinchGestureType = PinchGestureInput::PINCHGESTURE_END;
      mGestureState = eGestureState_None;
      break;
    }
    default: {
      NS_WARNING("Unexpected phase for pinch gesture event.");
      return;
    }
  }

  PinchGestureInput event{pinchGestureType,
                          PinchGestureInput::TRACKPAD,
                          eventTimeStamp,
                          screenOffset,
                          position,
                          100.0,
                          100.0 * (1.0 - [anEvent magnification]),
                          nsCocoaUtils::ModifiersForEvent(anEvent)};

  mGeckoChild->DispatchAPZInputEvent(event);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Smart zoom gesture, i.e. two-finger double tap on trackpads.
- (void)smartMagnifyWithEvent:(NSEvent*)anEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!anEvent || !mGeckoChild ||
      [self beginOrEndGestureForEventPhase:anEvent]) {
    return;
  }

  if ([self maybeRollup:anEvent]) {
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  if (StaticPrefs::apz_mac_enable_double_tap_zoom_touchpad_gesture()) {
    TimeStamp eventTimeStamp =
        nsCocoaUtils::GetEventTimeStamp([anEvent timestamp]);
    NSPoint locationInWindow =
        nsCocoaUtils::EventLocationForWindow(anEvent, [self window]);
    LayoutDevicePoint position =
        [self convertWindowCoordinatesRoundDown:locationInWindow];

    mGeckoChild->DispatchDoubleTapGesture(
        eventTimeStamp, RoundedToInt(position),
        nsCocoaUtils::ModifiersForEvent(anEvent));
  } else {
    // Setup the "double tap" event.
    WidgetSimpleGestureEvent geckoEvent(true, eTapGesture, mGeckoChild);
    [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];
    geckoEvent.mClickCount = 1;

    // Send the event.
    mGeckoChild->DispatchWindowEvent(geckoEvent);
  }

  // Clear the gesture state
  mGestureState = eGestureState_None;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)rotateWithEvent:(NSEvent*)anEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!anEvent || !mGeckoChild ||
      [self beginOrEndGestureForEventPhase:anEvent]) {
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  float rotation = [anEvent rotation];

  EventMessage msg;
  switch (mGestureState) {
    case eGestureState_StartGesture:
      msg = eRotateGestureStart;
      mGestureState = eGestureState_RotateGesture;
      break;

    case eGestureState_RotateGesture:
      msg = eRotateGestureUpdate;
      break;

    case eGestureState_None:
    case eGestureState_MagnifyGesture:
    default:
      return;
  }

  // Setup the event.
  WidgetSimpleGestureEvent geckoEvent(true, msg, mGeckoChild);
  [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];
  geckoEvent.mDelta = -rotation;
  if (rotation > 0.0) {
    geckoEvent.mDirection =
        dom::SimpleGestureEvent_Binding::ROTATION_COUNTERCLOCKWISE;
  } else {
    geckoEvent.mDirection = dom::SimpleGestureEvent_Binding::ROTATION_CLOCKWISE;
  }

  // Send the event.
  mGeckoChild->DispatchWindowEvent(geckoEvent);

  // Keep track of the cumulative rotation for the final "rotate" event.
  mCumulativeRotation += rotation;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// `beginGestureWithEvent` and `endGestureWithEvent` are not called for
// applications that link against the macOS 10.11 or later SDK when we're
// running on macOS 10.11 or later. For compatibility with all supported macOS
// versions, we have to call {begin,end}GestureWithEvent ourselves based on
// the event phase when we're handling gestures.
- (bool)beginOrEndGestureForEventPhase:(NSEvent*)aEvent {
  if (!aEvent) {
    return false;
  }

  if (aEvent.phase == NSEventPhaseBegan) {
    [self beginGestureWithEvent:aEvent];
    return true;
  }

  if (aEvent.phase == NSEventPhaseEnded ||
      aEvent.phase == NSEventPhaseCancelled) {
    [self endGestureWithEvent:aEvent];
    return true;
  }

  return false;
}

- (void)beginGestureWithEvent:(NSEvent*)aEvent {
  if (!aEvent) {
    return;
  }

  mGestureState = eGestureState_StartGesture;
  mCumulativeRotation = 0.0;
}

- (void)endGestureWithEvent:(NSEvent*)anEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!anEvent || !mGeckoChild) {
    // Clear the gestures state if we cannot send an event.
    mGestureState = eGestureState_None;
    mCumulativeRotation = 0.0;
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  switch (mGestureState) {
    case eGestureState_RotateGesture: {
      // Setup the "rotate" event.
      WidgetSimpleGestureEvent geckoEvent(true, eRotateGesture, mGeckoChild);
      [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];
      geckoEvent.mDelta = -mCumulativeRotation;
      if (mCumulativeRotation > 0.0) {
        geckoEvent.mDirection =
            dom::SimpleGestureEvent_Binding::ROTATION_COUNTERCLOCKWISE;
      } else {
        geckoEvent.mDirection =
            dom::SimpleGestureEvent_Binding::ROTATION_CLOCKWISE;
      }

      // Send the event.
      mGeckoChild->DispatchWindowEvent(geckoEvent);
    } break;

    case eGestureState_MagnifyGesture:  // APZ handles sending the widget events
    case eGestureState_None:
    case eGestureState_StartGesture:
    default:
      break;
  }

  // Clear the gestures state.
  mGestureState = eGestureState_None;
  mCumulativeRotation = 0.0;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Returning NO from this method only disallows ordering on mousedown - in order
// to prevent it for mouseup too, we need to call [NSApp preventWindowOrdering]
// when handling the mousedown event.
- (BOOL)shouldDelayWindowOrderingForEvent:(NSEvent*)aEvent {
  // Always using system-provided window ordering for normal windows.
  if (![[self window] isKindOfClass:[PopupWindow class]]) return NO;

  // Don't reorder when we don't have a parent window, like when we're a
  // context menu or a tooltip.
  return ![[self window] parentWindow];
}

- (void)mouseDown:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;
  mPerformedDrag = NO;

  if ([self shouldDelayWindowOrderingForEvent:theEvent]) {
    [NSApp preventWindowOrdering];
  }

  // If we've already seen this event due to direct dispatch from menuForEvent:
  // just bail; if not, remember it.
  if (mLastMouseDownEvent == theEvent) {
    [mLastMouseDownEvent release];
    mLastMouseDownEvent = nil;
    return;
  } else {
    [mLastMouseDownEvent release];
    mLastMouseDownEvent = [theEvent retain];
  }

  [gLastDragMouseDownEvent release];
  gLastDragMouseDownEvent = [theEvent retain];
  gLastDragView = self;

  // We need isClickThrough because at this point the window we're in might
  // already have become main, so the check for isMainWindow in
  // WindowAcceptsEvent isn't enough. It also has to check isClickThrough.
  BOOL isClickThrough = (theEvent == mClickThroughMouseDownEvent);
  [mClickThroughMouseDownEvent release];
  mClickThroughMouseDownEvent = nil;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  if ([self maybeRollup:theEvent] ||
      !ChildViewMouseTracker::WindowAcceptsEvent([self window], theEvent, self,
                                                 isClickThrough)) {
    // Remember blocking because that means we want to block mouseup as well.
    mBlockedLastMouseDown = YES;
    return;
  }

  // in order to send gecko events we'll need a gecko widget
  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  WidgetMouseEvent geckoEvent(true, eMouseDown, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];

  NSInteger clickCount = [theEvent clickCount];
  if (mBlockedLastMouseDown && clickCount > 1) {
    // Don't send a double click if the first click of the double click was
    // blocked.
    clickCount--;
  }
  geckoEvent.mClickCount = clickCount;

  if (!StaticPrefs::dom_event_treat_ctrl_click_as_right_click_disabled() &&
      geckoEvent.IsControl()) {
    geckoEvent.mButton = MouseButton::eSecondary;
  } else {
    geckoEvent.mButton = MouseButton::ePrimary;
    // Don't send a click if ctrl key is pressed.
    geckoEvent.mClickEventPrevented = geckoEvent.IsControl();
  }

  mGeckoChild->DispatchInputEvent(&geckoEvent);
  mBlockedLastMouseDown = NO;

  // XXX maybe call markedTextSelectionChanged:client: here?

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)mouseUp:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  gLastDragView = nil;

  if (!mGeckoChild || mBlockedLastMouseDown || mPerformedDrag) {
    // There is case that mouseUp event will be fired right after DnD on macOS.
    // As mPerformedDrag will be YES at end of DnD processing, ignore this
    // mouseUp event fired right after DnD.
    return;
  }

  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  WidgetMouseEvent geckoEvent(true, eMouseUp, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];

  if (!StaticPrefs::dom_event_treat_ctrl_click_as_right_click_disabled() &&
      ([theEvent modifierFlags] & NSEventModifierFlagControl)) {
    geckoEvent.mButton = MouseButton::eSecondary;
  } else {
    geckoEvent.mButton = MouseButton::ePrimary;
  }

  // Remember the event's position before calling DispatchInputEvent, because
  // that call can mutate it and convert it into a different coordinate space.
  LayoutDeviceIntPoint pos = geckoEvent.mRefPoint;

  // This might destroy our widget (and null out mGeckoChild).
  bool defaultPrevented =
      (mGeckoChild->DispatchInputEvent(&geckoEvent).mContentStatus ==
       nsEventStatus_eConsumeNoDefault);

  if (!mGeckoChild) {
    return;
  }

  // Check to see if we are double-clicking in draggable parts of the window.
  if (!defaultPrevented && [theEvent clickCount] == 2 &&
      !mGeckoChild->GetNonDraggableRegion().Contains(pos.x, pos.y)) {
    if (nsCocoaUtils::ShouldZoomOnTitlebarDoubleClick()) {
      [[self window] performZoom:nil];
    } else if (nsCocoaUtils::ShouldMinimizeOnTitlebarDoubleClick()) {
      [[self window] performMiniaturize:nil];
    }
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)sendMouseEnterOrExitEvent:(NSEvent*)aEvent
                            enter:(BOOL)aEnter
                         exitFrom:(WidgetMouseEvent::ExitFrom)aExitFrom {
  if (!mGeckoChild) return;

  NSPoint windowEventLocation =
      nsCocoaUtils::EventLocationForWindow(aEvent, [self window]);
  NSPoint localEventLocation = [self convertPoint:windowEventLocation
                                         fromView:nil];

  EventMessage msg = aEnter ? eMouseEnterIntoWidget : eMouseExitFromWidget;
  WidgetMouseEvent event(true, msg, mGeckoChild, WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:aEvent toGeckoEvent:&event];
  event.mRefPoint = mGeckoChild->CocoaPointsToDevPixels(localEventLocation);
  if (event.mMessage == eMouseExitFromWidget) {
    event.mExitFrom = Some(aExitFrom);
  }
  mGeckoChild->DispatchEvent(&event);
}

- (void)handleMouseMoved:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  WidgetMouseEvent geckoEvent(true, eMouseMove, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];

  mGeckoChild->DispatchInputEvent(&geckoEvent);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)mouseDragged:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  WidgetMouseEvent geckoEvent(true, eMouseMove, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];

  mGeckoChild->DispatchInputEvent(&geckoEvent);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)rightMouseDown:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;
  mPerformedDrag = NO;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  [self maybeRollup:theEvent];
  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  // The right mouse went down, fire off a right mouse down event to gecko
  WidgetMouseEvent geckoEvent(true, eMouseDown, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.mButton = MouseButton::eSecondary;
  geckoEvent.mClickCount = [theEvent clickCount];

  nsIWidget::ContentAndAPZEventStatus eventStatus =
      mGeckoChild->DispatchInputEvent(&geckoEvent);
  if (!mGeckoChild) return;

  if (!StaticPrefs::ui_context_menus_after_mouseup() &&
      eventStatus.mApzStatus != nsEventStatus_eConsumeNoDefault) {
    // Let the superclass do the context menu stuff.
    [super rightMouseDown:theEvent];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)rightMouseUp:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  WidgetMouseEvent geckoEvent(true, eMouseUp, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.mButton = MouseButton::eSecondary;
  geckoEvent.mClickCount = [theEvent clickCount];

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  nsIWidget::ContentAndAPZEventStatus eventStatus =
      mGeckoChild->DispatchInputEvent(&geckoEvent);
  if (!mGeckoChild) return;

  if (StaticPrefs::ui_context_menus_after_mouseup() &&
      eventStatus.mApzStatus != nsEventStatus_eConsumeNoDefault) {
    // Let the superclass do the context menu stuff, but pretend it's
    // rightMouseDown.
    NSEvent* dupeEvent = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                                            location:theEvent.locationInWindow
                                       modifierFlags:theEvent.modifierFlags
                                           timestamp:theEvent.timestamp
                                        windowNumber:theEvent.windowNumber
                                             context:nil
                                         eventNumber:theEvent.eventNumber
                                          clickCount:theEvent.clickCount
                                            pressure:theEvent.pressure];

    [super rightMouseDown:dupeEvent];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)rightMouseDragged:(NSEvent*)theEvent {
  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  WidgetMouseEvent geckoEvent(true, eMouseMove, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.mButton = MouseButton::eSecondary;

  mGeckoChild->DispatchInputEvent(&geckoEvent);
}

static bool ShouldDispatchBackForwardCommandForMouseButton(int16_t aButton) {
  return (aButton == MouseButton::eX1 &&
          Preferences::GetBool("mousebutton.4th.enabled", true)) ||
         (aButton == MouseButton::eX2 &&
          Preferences::GetBool("mousebutton.5th.enabled", true));
}

- (void)otherMouseDown:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;
  mPerformedDrag = NO;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  if ([self maybeRollup:theEvent] ||
      !ChildViewMouseTracker::WindowAcceptsEvent([self window], theEvent, self))
    return;

  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  int16_t button = nsCocoaUtils::ButtonForEvent(theEvent);
  if (ShouldDispatchBackForwardCommandForMouseButton(button)) {
    WidgetCommandEvent appCommandEvent(
        true,
        (button == MouseButton::eX2) ? nsGkAtoms::Forward : nsGkAtoms::Back,
        mGeckoChild);
    appCommandEvent.mTimeStamp =
        nsCocoaUtils::GetEventTimeStamp([theEvent timestamp]);
    mGeckoChild->DispatchWindowEvent(appCommandEvent);
    return;
  }

  WidgetMouseEvent geckoEvent(true, eMouseDown, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.mButton = button;
  geckoEvent.mClickCount = [theEvent clickCount];

  mGeckoChild->DispatchInputEvent(&geckoEvent);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)otherMouseUp:(NSEvent*)theEvent {
  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  int16_t button = nsCocoaUtils::ButtonForEvent(theEvent);
  if (ShouldDispatchBackForwardCommandForMouseButton(button)) {
    return;
  }

  WidgetMouseEvent geckoEvent(true, eMouseUp, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.mButton = button;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  mGeckoChild->DispatchInputEvent(&geckoEvent);
}

- (void)otherMouseDragged:(NSEvent*)theEvent {
  if (!mGeckoChild) return;
  if (mTextInputHandler->OnHandleEvent(theEvent)) {
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  WidgetMouseEvent geckoEvent(true, eMouseMove, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  int16_t button = nsCocoaUtils::ButtonForEvent(theEvent);
  geckoEvent.mButton = button;

  mGeckoChild->DispatchInputEvent(&geckoEvent);
}

- (void)sendWheelStartOrStop:(EventMessage)msg forEvent:(NSEvent*)theEvent {
  if (!mGeckoChild) {
    return;
  }

  WidgetWheelEvent wheelEvent(true, msg, mGeckoChild);
  [self convertCocoaMouseWheelEvent:theEvent toGeckoEvent:&wheelEvent];
  mExpectingWheelStop = (msg == eWheelOperationStart);
  mGeckoChild->DispatchInputEvent(wheelEvent.AsInputEvent());
}

- (void)sendWheelCondition:(BOOL)condition
                     first:(EventMessage)first
                    second:(EventMessage)second
                  forEvent:(NSEvent*)theEvent {
  if (mExpectingWheelStop == condition) {
    [self sendWheelStartOrStop:first forEvent:theEvent];
  }
  [self sendWheelStartOrStop:second forEvent:theEvent];
}

static int32_t RoundUp(double aDouble) {
  return aDouble < 0 ? static_cast<int32_t>(floor(aDouble))
                     : static_cast<int32_t>(ceil(aDouble));
}

static gfx::IntPoint GetIntegerDeltaForEvent(NSEvent* aEvent) {
  if ([aEvent hasPreciseScrollingDeltas]) {
    // Pixel scroll events (events with hasPreciseScrollingDeltas == YES)
    // carry pixel deltas in the scrollingDeltaX/Y fields and line scroll
    // information in the deltaX/Y fields.
    // Prior to 10.12, these line scroll fields would be zero for most pixel
    // scroll events and non-zero for some, whenever at least a full line
    // worth of pixel scrolling had accumulated. That's the behavior we want.
    // Starting with 10.12 however, pixel scroll events no longer accumulate
    // deltaX and deltaY; they just report floating point values for every
    // single event. So we need to do our own accumulation.
    return PanGestureInput::GetIntegerDeltaForEvent(
        [aEvent phase] == NSEventPhaseBegan, [aEvent deltaX], [aEvent deltaY]);
  }

  // For line scrolls, or pre-10.12, just use the rounded up value of deltaX /
  // deltaY.
  return gfx::IntPoint(RoundUp([aEvent deltaX]), RoundUp([aEvent deltaY]));
}

- (void)scrollWheel:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  ChildViewMouseTracker::MouseScrolled(theEvent);

  if ([self maybeRollup:theEvent]) {
    return;
  }

  if (!mGeckoChild) {
    return;
  }

  NSEventPhase phase = [theEvent phase];
  // Fire eWheelOperationStart/End events when 2 fingers touch/release the
  // touchpad.
  if (phase & NSEventPhaseMayBegin) {
    [self sendWheelCondition:YES
                       first:eWheelOperationEnd
                      second:eWheelOperationStart
                    forEvent:theEvent];
  } else if (phase & (NSEventPhaseEnded | NSEventPhaseCancelled)) {
    [self sendWheelCondition:NO
                       first:eWheelOperationStart
                      second:eWheelOperationEnd
                    forEvent:theEvent];
  }

  if (!mGeckoChild) {
    return;
  }
  RefPtr<nsCocoaWindow> geckoChildDeathGrip(mGeckoChild);

  NSPoint locationInWindow =
      nsCocoaUtils::EventLocationForWindow(theEvent, [self window]);

  // Use convertWindowCoordinatesRoundDown when converting the position to
  // integer screen pixels in order to ensure that coordinates which are just
  // inside the right / bottom edges of the window don't end up outside of the
  // window after rounding.
  ScreenPoint position = ViewAs<ScreenPixel>(
      [self convertWindowCoordinatesRoundDown:locationInWindow],
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);

  bool usePreciseDeltas =
      [theEvent hasPreciseScrollingDeltas] &&
      Preferences::GetBool("mousewheel.enable_pixel_scrolling", true);
  bool hasPhaseInformation = nsCocoaUtils::EventHasPhaseInformation(theEvent);

  gfx::IntPoint lineOrPageDelta = -GetIntegerDeltaForEvent(theEvent);

  Modifiers modifiers = nsCocoaUtils::ModifiersForEvent(theEvent);

  TimeStamp eventTimeStamp =
      nsCocoaUtils::GetEventTimeStamp([theEvent timestamp]);

  ScreenPoint preciseDelta;
  if (usePreciseDeltas) {
    CGFloat pixelDeltaX = [theEvent scrollingDeltaX];
    CGFloat pixelDeltaY = [theEvent scrollingDeltaY];
    double scale = geckoChildDeathGrip->BackingScaleFactor();
    preciseDelta = ScreenPoint(-pixelDeltaX * scale, -pixelDeltaY * scale);
  }

  if (usePreciseDeltas && hasPhaseInformation) {
    PanGestureInput panEvent = nsCocoaUtils::CreatePanGestureEvent(
        theEvent, eventTimeStamp, position, preciseDelta, lineOrPageDelta,
        modifiers);

    geckoChildDeathGrip->DispatchAPZWheelInputEvent(panEvent);
  } else if (usePreciseDeltas) {
    // This is on 10.6 or old touchpads that don't have any phase information.
    ScrollWheelInput wheelEvent(eventTimeStamp, modifiers,
                                ScrollWheelInput::SCROLLMODE_INSTANT,
                                ScrollWheelInput::SCROLLDELTA_PIXEL, position,
                                preciseDelta.x, preciseDelta.y, false,
                                // This parameter is used for wheel delta
                                // adjustment, such as auto-dir scrolling,
                                // but we do't need to do anything special here
                                // since this wheel event is sent to
                                // DispatchAPZWheelInputEvent, which turns this
                                // ScrollWheelInput back into a WidgetWheelEvent
                                // and then it goes through the regular handling
                                // in APZInputBridge. So passing |eNone| won't
                                // pass up the necessary wheel delta adjustment.
                                WheelDeltaAdjustmentStrategy::eNone);
    wheelEvent.mLineOrPageDeltaX = lineOrPageDelta.x;
    wheelEvent.mLineOrPageDeltaY = lineOrPageDelta.y;
    wheelEvent.mIsMomentum = nsCocoaUtils::IsMomentumScrollEvent(theEvent);
    geckoChildDeathGrip->DispatchAPZWheelInputEvent(wheelEvent);
  } else {
    ScrollWheelInput::ScrollMode scrollMode =
        ScrollWheelInput::SCROLLMODE_INSTANT;
    if (nsLayoutUtils::IsSmoothScrollingEnabled() &&
        StaticPrefs::general_smoothScroll_mouseWheel()) {
      scrollMode = ScrollWheelInput::SCROLLMODE_SMOOTH;
    }
    ScrollWheelInput wheelEvent(eventTimeStamp, modifiers, scrollMode,
                                ScrollWheelInput::SCROLLDELTA_LINE, position,
                                lineOrPageDelta.x, lineOrPageDelta.y, false,
                                // This parameter is used for wheel delta
                                // adjustment, such as auto-dir scrolling,
                                // but we do't need to do anything special here
                                // since this wheel event is sent to
                                // DispatchAPZWheelInputEvent, which turns this
                                // ScrollWheelInput back into a WidgetWheelEvent
                                // and then it goes through the regular handling
                                // in APZInputBridge. So passing |eNone| won't
                                // pass up the necessary wheel delta adjustment.
                                WheelDeltaAdjustmentStrategy::eNone);
    wheelEvent.mLineOrPageDeltaX = lineOrPageDelta.x;
    wheelEvent.mLineOrPageDeltaY = lineOrPageDelta.y;
    geckoChildDeathGrip->DispatchAPZWheelInputEvent(wheelEvent);
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (NSMenu*)menuForEvent:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!mGeckoChild) return nil;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  [self maybeRollup:theEvent];
  if (!mGeckoChild) return nil;

  // Cocoa doesn't always dispatch a mouseDown: for a control-click event,
  // depends on what we return from menuForEvent:. Gecko always expects one
  // and expects the mouse down event before the context menu event, so
  // get that event sent first if this is a left mouse click.
  if ([theEvent type] == NSEventTypeLeftMouseDown) {
    [self mouseDown:theEvent];
    if (!mGeckoChild) return nil;
  }

  WidgetPointerEvent geckoEvent(true, eContextMenu, mGeckoChild);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  if (StaticPrefs::dom_event_treat_ctrl_click_as_right_click_disabled() &&
      [theEvent type] == NSEventTypeLeftMouseDown) {
    geckoEvent.mContextMenuTrigger = WidgetMouseEvent::eControlClick;
    geckoEvent.mButton = MouseButton::ePrimary;
  } else {
    geckoEvent.mButton = MouseButton::eSecondary;
  }

  mGeckoChild->DispatchInputEvent(&geckoEvent);
  if (!mGeckoChild) return nil;

  [self maybeInitContextMenuTracking];

  // We never return an actual NSMenu* for the context menu. Gecko might have
  // responded to the eContextMenu event by putting up a fake context menu.
  return nil;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (void)willOpenMenu:(NSMenu*)aMenu withEvent:(NSEvent*)aEvent {
  ChildViewMouseTracker::NativeMenuOpened();
}

- (void)didCloseMenu:(NSMenu*)aMenu withEvent:(NSEvent*)aEvent {
  ChildViewMouseTracker::NativeMenuClosed();
}

- (void)convertCocoaMouseWheelEvent:(NSEvent*)aMouseEvent
                       toGeckoEvent:(WidgetWheelEvent*)outWheelEvent {
  [self convertCocoaMouseEvent:aMouseEvent toGeckoEvent:outWheelEvent];

  bool usePreciseDeltas =
      [aMouseEvent hasPreciseScrollingDeltas] &&
      Preferences::GetBool("mousewheel.enable_pixel_scrolling", true);

  outWheelEvent->mDeltaMode = usePreciseDeltas
                                  ? dom::WheelEvent_Binding::DOM_DELTA_PIXEL
                                  : dom::WheelEvent_Binding::DOM_DELTA_LINE;
  outWheelEvent->mIsMomentum = nsCocoaUtils::IsMomentumScrollEvent(aMouseEvent);
}

- (void)convertCocoaMouseEvent:(NSEvent*)aMouseEvent
                  toGeckoEvent:(WidgetInputEvent*)outGeckoEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  NS_ASSERTION(
      outGeckoEvent,
      "convertCocoaMouseEvent:toGeckoEvent: requires non-null aoutGeckoEvent");
  if (!outGeckoEvent) return;

  nsCocoaUtils::InitInputEvent(*outGeckoEvent, aMouseEvent);

  // convert point to view coordinate system
  NSPoint locationInWindow =
      nsCocoaUtils::EventLocationForWindow(aMouseEvent, [self window]);

  // If the pointer is locked, we need to track the reference point ourselves,
  // because EventStateManager uses the mouse event's mRefPoint to determine
  // whether the pointer needs to be re-centered.
  if (const auto& nativePointerLockMode =
          nsCocoaWindow::GetNativePointerLockedMode()) {
    outGeckoEvent->mRefPoint = nsCocoaWindow::GetNativeLockedPoint();
    WidgetMouseEvent* widgetMouseEvent = outGeckoEvent->AsMouseEvent();
    if (widgetMouseEvent && widgetMouseEvent->mMessage == eMouseMove) {
      Maybe<LayoutDeviceIntPoint> movement;
      // If pointer lock is active with |unadjustedMovement: true|, source
      // unaccelerated mouse delta directly from the underlying CGEvent and
      // stash it on the WidgetMouseEvent. MouseEvent::movementX/Y will then
      // return this value verbatim instead of computing a delta from the warped
      // cursor position, which carries OS mouse acceleration.
      // https://w3c.github.io/pointerlock/#pointerlockoptions-dictionary
      if (*nativePointerLockMode ==
          nsIWidget::NativePointerLockMode::Unadjusted) {
        if (CGEventRef cgEvent = [aMouseEvent CGEvent]) {
          movement.emplace(
              int32_t(CGEventGetIntegerValueField(
                  cgEvent, kCGEventUnacceleratedPointerMovementX)),
              int32_t(CGEventGetIntegerValueField(
                  cgEvent, kCGEventUnacceleratedPointerMovementY)));
        }
      }

      // Fallback to use the deltaX/Y if we don't request unadjusted movement or
      // fail to get the unadjusted movement from the CGEvent.
      if (!movement) {
        movement.emplace(int32_t(aMouseEvent.deltaX),
                         int32_t(aMouseEvent.deltaY));
      }

      widgetMouseEvent->mMovement = std::move(movement);
    }
  } else {
    outGeckoEvent->mRefPoint = [self convertWindowCoordinates:locationInWindow];
  }

  WidgetMouseEventBase* mouseEvent = outGeckoEvent->AsMouseEventBase();
  mouseEvent->mButtons = 0;
  NSUInteger mouseButtons = [NSEvent pressedMouseButtons];

  if (mouseButtons & 0x01) {
    mouseEvent->mButtons |= MouseButtonsFlag::ePrimaryFlag;
  }
  if (mouseButtons & 0x02) {
    mouseEvent->mButtons |= MouseButtonsFlag::eSecondaryFlag;
  }
  if (mouseButtons & 0x04) {
    mouseEvent->mButtons |= MouseButtonsFlag::eMiddleFlag;
  }
  if (mouseButtons & 0x08) {
    mouseEvent->mButtons |= MouseButtonsFlag::e4thFlag;
  }
  if (mouseButtons & 0x10) {
    mouseEvent->mButtons |= MouseButtonsFlag::e5thFlag;
  }

  switch ([aMouseEvent type]) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp:
    case NSEventTypeOtherMouseDragged:
    case NSEventTypeMouseMoved:
      if ([aMouseEvent subtype] == NSEventSubtypeTabletPoint) {
        [self convertCocoaTabletPointerEvent:aMouseEvent
                                toGeckoEvent:mouseEvent->AsMouseEvent()];
      }
      break;

    default:
      // Don't check other NSEvents for pressure.
      break;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)convertCocoaTabletPointerEvent:(NSEvent*)aPointerEvent
                          toGeckoEvent:(WidgetMouseEvent*)aOutGeckoEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK
  if (!aOutGeckoEvent || !sIsTabletPointerActivated) {
    return;
  }
  if ([aPointerEvent type] != NSEventTypeMouseMoved) {
    aOutGeckoEvent->mPressure = [aPointerEvent pressure];
    MOZ_ASSERT(aOutGeckoEvent->mPressure >= 0.0 &&
               aOutGeckoEvent->mPressure <= 1.0);
  }
  aOutGeckoEvent->mInputSource = dom::MouseEvent_Binding::MOZ_SOURCE_PEN;
  aOutGeckoEvent->mTilt.emplace((int32_t)lround([aPointerEvent tilt].x * 90),
                                (int32_t)lround([aPointerEvent tilt].y * 90));
  aOutGeckoEvent->tangentialPressure = [aPointerEvent tangentialPressure];
  // Make sure the twist value is in the range of 0-359.
  int32_t twist = (int32_t)fmod([aPointerEvent rotation], 360);
  aOutGeckoEvent->twist = twist >= 0 ? twist : twist + 360;
  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)tabletProximity:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK
  sIsTabletPointerActivated = [theEvent isEnteringProximity];
  NS_OBJC_END_TRY_IGNORE_BLOCK
}

#pragma mark -
// NSTextInputClient implementation

- (NSRange)markedRange {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NS_ENSURE_TRUE(mTextInputHandler, NSMakeRange(NSNotFound, 0));
  return mTextInputHandler->MarkedRange();

  NS_OBJC_END_TRY_BLOCK_RETURN(NSMakeRange(0, 0));
}

- (NSRange)selectedRange {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NS_ENSURE_TRUE(mTextInputHandler, NSMakeRange(NSNotFound, 0));
  return mTextInputHandler->SelectedRange();

  NS_OBJC_END_TRY_BLOCK_RETURN(NSMakeRange(0, 0));
}

- (BOOL)drawsVerticallyForCharacterAtIndex:(NSUInteger)charIndex {
  NS_ENSURE_TRUE(mTextInputHandler, NO);
  if (charIndex == NSNotFound) {
    return NO;
  }
  return mTextInputHandler->DrawsVerticallyForCharacterAtIndex(charIndex);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)thePoint {
  NS_ENSURE_TRUE(mTextInputHandler, 0);
  return mTextInputHandler->CharacterIndexForPoint(thePoint);
}

- (NSArray*)validAttributesForMarkedText {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NS_ENSURE_TRUE(mTextInputHandler, [NSArray array]);
  return mTextInputHandler->GetValidAttributesForMarkedText();

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (void)insertText:(id)aString replacementRange:(NSRange)replacementRange {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  NS_ENSURE_TRUE_VOID(mGeckoChild);

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  NSString* str;
  if ([aString isKindOfClass:[NSAttributedString class]]) {
    str = [aString string];
  } else {
    str = aString;
  }

  mTextInputHandler->InsertText(str, &replacementRange);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)doCommandBySelector:(SEL)aSelector {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mGeckoChild || !mTextInputHandler) {
    return;
  }

  const char* sel = reinterpret_cast<const char*>(aSelector);
  if (!mTextInputHandler->DoCommandBySelector(sel)) {
    [super doCommandBySelector:aSelector];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)unmarkText {
  NS_ENSURE_TRUE_VOID(mTextInputHandler);
  mTextInputHandler->CommitIMEComposition();
}

- (BOOL)hasMarkedText {
  NS_ENSURE_TRUE(mTextInputHandler, NO);
  return mTextInputHandler->HasMarkedText();
}

- (void)setMarkedText:(id)aString
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  NS_ENSURE_TRUE_VOID(mTextInputHandler);

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  NSAttributedString* attrStr;
  if ([aString isKindOfClass:[NSAttributedString class]]) {
    attrStr = static_cast<NSAttributedString*>(aString);
  } else {
    attrStr = [[[NSAttributedString alloc] initWithString:aString] autorelease];
  }

  mTextInputHandler->SetMarkedText(attrStr, selectedRange, &replacementRange);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)aRange
                                               actualRange:
                                                   (NSRangePointer)actualRange {
  NS_ENSURE_TRUE(mTextInputHandler, nil);
  return mTextInputHandler->GetAttributedSubstringFromRange(aRange,
                                                            actualRange);
}

- (NSRect)firstRectForCharacterRange:(NSRange)aRange
                         actualRange:(NSRangePointer)actualRange {
  NS_ENSURE_TRUE(mTextInputHandler, NSMakeRect(0.0, 0.0, 0.0, 0.0));
  return mTextInputHandler->FirstRectForCharacterRange(aRange, actualRange);
}

- (void)quickLookWithEvent:(NSEvent*)event {
  if (!mGeckoChild) {
    return;
  }

  WidgetContentCommandEvent contentCommandEvent(
      true, eContentCommandLookUpDictionary, mGeckoChild);
  contentCommandEvent.mTimeStamp =
      nsCocoaUtils::GetEventTimeStamp([event timestamp]);
  NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
  contentCommandEvent.mRefPoint = mGeckoChild->CocoaPointsToDevPixels(point);
  mGeckoChild->DispatchWindowEvent(contentCommandEvent);
}

- (NSInteger)windowLevel {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NS_ENSURE_TRUE(mTextInputHandler, [[self window] level]);
  return mTextInputHandler->GetWindowLevel();

  NS_OBJC_END_TRY_BLOCK_RETURN(NSNormalWindowLevel);
}

#pragma mark -

// This is a private API that Cocoa uses.
// Cocoa will call this after the menu system returns "NO" for
// "performKeyEquivalent:". We want all they key events we can get so just
// return YES. In particular, this fixes ctrl-tab - we don't get a "keyDown:"
// call for that without this.
- (BOOL)_wantsKeyDownForEvent:(NSEvent*)event {
  return YES;
}

- (NSEvent*)lastKeyDownEvent {
  return mLastKeyDownEvent;
}

- (void)keyDown:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  [mLastKeyDownEvent release];
  mLastKeyDownEvent = [theEvent retain];

  // Weird things can happen on keyboard input if the key window isn't in the
  // current space.  For example see bug 1056251.  To get around this, always
  // make sure that, if our window is key, it's also made frontmost.  Doing
  // this automatically switches to whatever space our window is in.  Safari
  // does something similar.  Our window should normally always be key --
  // otherwise why is the OS sending us a key down event?  But it's just
  // possible we're in Gecko's hidden window, so we check first.
  //
  // Skip this if the window is in the middle of an async transition (e.g.
  // miniaturize). Calling orderWindow: on a miniaturizing window can cancel
  // the animation, preventing windowDidMiniaturize: from firing and leaving
  // the transition state machine permanently stuck.
  NSWindow* viewWindow = [self window];
  if (viewWindow && [viewWindow isKeyWindow]) {
    bool isInTransition = false;
    id delegate = [viewWindow delegate];
    if ([delegate isKindOfClass:[WindowDelegate class]]) {
      if (nsCocoaWindow* geckoWindow =
              [(WindowDelegate*)delegate geckoWidget]) {
        isInTransition = geckoWindow->IsInTransition();
      }
    }
    if (!isInTransition) {
      [viewWindow orderWindow:NSWindowAbove relativeTo:0];
    }
  }

#if !defined(RELEASE_OR_BETA) || defined(DEBUG)
  if (!Preferences::GetBool("intl.allow-insecure-text-input", false) &&
      mGeckoChild && mTextInputHandler && mTextInputHandler->IsFocused()) {
    NSWindow* window = [self window];
    NSString* info = [NSString
        stringWithFormat:@"\nview [%@], window [%@], window is key %i, is "
                         @"fullscreen %i, app is active %i",
                         self, window, [window isKeyWindow],
                         ([window styleMask] & NSWindowStyleMaskFullScreen) !=
                             0,
                         [NSApp isActive]];
    nsAutoCString additionalInfo([info UTF8String]);

    if (mGeckoChild->GetInputContext().IsPasswordEditor() &&
        !TextInputHandler::IsSecureEventInputEnabled()) {
#  define CRASH_MESSAGE \
    "A password editor has focus, but not in secure input mode"

      CrashReporter::AppendAppNotesToCrashReport(
          "\nBug 893973: "_ns + nsLiteralCString(CRASH_MESSAGE));
      CrashReporter::AppendAppNotesToCrashReport(additionalInfo);

      MOZ_CRASH(CRASH_MESSAGE);
#  undef CRASH_MESSAGE
    } else if (!mGeckoChild->GetInputContext().IsPasswordEditor() &&
               TextInputHandler::IsSecureEventInputEnabled()) {
#  define CRASH_MESSAGE \
    "A non-password editor has focus, but in secure input mode"

      CrashReporter::AppendAppNotesToCrashReport(
          "\nBug 893973: "_ns + nsLiteralCString(CRASH_MESSAGE));
      CrashReporter::AppendAppNotesToCrashReport(additionalInfo);

      MOZ_CRASH(CRASH_MESSAGE);
#  undef CRASH_MESSAGE
    }
  }
#endif  // #if !defined(RELEASE_OR_BETA) || defined(DEBUG)

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  if (mGeckoChild) {
    if (mTextInputHandler) {
      sUniqueKeyEventId++;
      NSMutableDictionary* nativeKeyEventsMap = [ChildView sNativeKeyEventsMap];
      [nativeKeyEventsMap setObject:theEvent forKey:@(sUniqueKeyEventId)];
      // Purge old native events, in case we're still holding on to them. We
      // keep at most 10 references to 10 different native events.
      [nativeKeyEventsMap removeObjectForKey:@(sUniqueKeyEventId - 10)];
      mTextInputHandler->HandleKeyDownEvent(theEvent, sUniqueKeyEventId);
    } else {
      // There was no text input handler. Offer the event to the native menu
      // system to check if there are any registered custom shortcuts for this
      // event.
      mGeckoChild->SendEventToNativeMenuSystem(theEvent);
    }
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)keyUp:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  NS_ENSURE_TRUE(mGeckoChild, );

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  mTextInputHandler->HandleKeyUpEvent(theEvent);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)insertNewline:(id)sender {
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::InsertParagraph);
  }
}

- (void)insertLineBreak:(id)sender {
  // Ctrl + Enter in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::InsertLineBreak);
  }
}

- (void)deleteBackward:(id)sender {
  // Backspace in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::DeleteCharBackward);
  }
}

- (void)deleteBackwardByDecomposingPreviousCharacter:(id)sender {
  // Ctrl + Backspace in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::DeleteCharBackward);
  }
}

- (void)deleteWordBackward:(id)sender {
  // Alt + Backspace in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::DeleteWordBackward);
  }
}

- (void)deleteToBeginningOfBackward:(id)sender {
  // Command + Backspace in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::DeleteToBeginningOfLine);
  }
}

- (void)deleteForward:(id)sender {
  // Delete in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::DeleteCharForward);
  }
}

- (void)deleteWordForward:(id)sender {
  // Alt + Delete in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::DeleteWordForward);
  }
}

- (void)insertTab:(id)sender {
  // Tab in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::InsertTab);
  }
}

- (void)insertBacktab:(id)sender {
  // Shift + Tab in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::InsertBacktab);
  }
}

- (void)moveRight:(id)sender {
  // RightArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::CharNext);
  }
}

- (void)moveRightAndModifySelection:(id)sender {
  // Shift + RightArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectCharNext);
  }
}

- (void)moveWordRight:(id)sender {
  // Alt + RightArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::WordNext);
  }
}

- (void)moveWordRightAndModifySelection:(id)sender {
  // Alt + Shift + RightArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectWordNext);
  }
}

- (void)moveToRightEndOfLine:(id)sender {
  // Command + RightArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::MoveRight3);
  }
}

- (void)moveToRightEndOfLineAndModifySelection:(id)sender {
  // Command + Shift + RightArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectRight3);
  }
}

- (void)moveLeft:(id)sender {
  // LeftArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::CharPrevious);
  }
}

- (void)moveLeftAndModifySelection:(id)sender {
  // Shift + LeftArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectCharPrevious);
  }
}

- (void)moveWordLeft:(id)sender {
  // Alt + LeftArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::WordPrevious);
  }
}

- (void)moveWordLeftAndModifySelection:(id)sender {
  // Alt + Shift + LeftArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectWordPrevious);
  }
}

- (void)moveToLeftEndOfLine:(id)sender {
  // Command + LeftArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::MoveLeft3);
  }
}

- (void)moveToLeftEndOfLineAndModifySelection:(id)sender {
  // Command + Shift + LeftArrow in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectLeft3);
  }
}

- (void)moveUp:(id)sender {
  // ArrowUp in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::LinePrevious);
  }
}

- (void)moveUpAndModifySelection:(id)sender {
  // Shift + ArrowUp in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectLinePrevious);
  }
}

- (void)moveToBeginningOfDocument:(id)sender {
  // Command + ArrowUp in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::MoveTop);
  }
}

- (void)moveToBeginningOfDocumentAndModifySelection:(id)sender {
  // Command + Shift + ArrowUp or Shift + Home in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectTop);
  }
}

- (void)moveDown:(id)sender {
  // ArrowDown in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::LineNext);
  }
}

- (void)moveDownAndModifySelection:(id)sender {
  // Shift + ArrowDown in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectLineNext);
  }
}

- (void)moveToEndOfDocument:(id)sender {
  // Command + ArrowDown in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::MoveBottom);
  }
}

- (void)moveToEndOfDocumentAndModifySelection:(id)sender {
  // Command + Shift + ArrowDown or Shift + End in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectBottom);
  }
}

- (void)scrollPageUp:(id)sender {
  // PageUp in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::ScrollPageUp);
  }
}

- (void)pageUpAndModifySelection:(id)sender {
  // Shift + PageUp in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectPageUp);
  }
}

- (void)scrollPageDown:(id)sender {
  // PageDown in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::ScrollPageDown);
  }
}

- (void)pageDownAndModifySelection:(id)sender {
  // Shift + PageDown in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::SelectPageDown);
  }
}

- (void)scrollToEndOfDocument:(id)sender {
  // End in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::ScrollBottom);
  }
}

- (void)scrollToBeginningOfDocument:(id)sender {
  // Home in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::ScrollTop);
  }
}

// XXX Don't decleare nor implement calcelOperation: because it
//     causes not calling keyDown: for Command + Period.
//     We need to handle it from doCommandBySelector:.

- (void)complete:(id)sender {
  // Alt + Escape or Alt + Shift + Escape in the default settings.
  if (mTextInputHandler) {
    mTextInputHandler->HandleCommand(Command::Complete);
  }
}

- (void)flagsChanged:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  NS_ENSURE_TRUE(mGeckoChild, );

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  mTextInputHandler->HandleFlagsChanged(theEvent);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (BOOL)isFirstResponder {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSResponder* resp = [[self window] firstResponder];
  return (resp == (NSResponder*)self);

  NS_OBJC_END_TRY_BLOCK_RETURN(NO);
}

- (BOOL)isDragInProgress {
  if (!mDragService) return NO;

  nsCOMPtr<nsIDragSession> dragSession =
      mDragService->GetCurrentSession(mGeckoChild);
  return dragSession != nullptr;
}

- (BOOL)inactiveWindowAcceptsMouseEvent:(NSEvent*)aEvent {
  // If we're being destroyed assume the default -- return YES.
  if (!mGeckoChild) return YES;

  WidgetMouseEvent geckoEvent(true, eMouseActivate, mGeckoChild,
                              WidgetMouseEvent::eReal);
  [self convertCocoaMouseEvent:aEvent toGeckoEvent:&geckoEvent];
  return (mGeckoChild->DispatchInputEvent(&geckoEvent).mContentStatus !=
          nsEventStatus_eConsumeNoDefault);
}

// We must always call through to our superclass, even when mGeckoChild is
// nil -- otherwise the keyboard focus can end up in the wrong NSView.
- (BOOL)becomeFirstResponder {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  return [super becomeFirstResponder];

  NS_OBJC_END_TRY_BLOCK_RETURN(YES);
}

- (void)viewsWindowDidBecomeKey {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mGeckoChild) return;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  // check to see if the window implements the mozWindow protocol. This
  // allows embedders to avoid re-entrant calls to -makeKeyAndOrderFront,
  // which can happen because these activate calls propagate out
  // to the embedder via nsIEmbeddingSiteWindow::SetFocus().
  BOOL isMozWindow =
      [[self window] respondsToSelector:@selector(setSuppressMakeKeyFront:)];
  if (isMozWindow) [[self window] setSuppressMakeKeyFront:YES];

  nsIWidgetListener* listener = mGeckoChild->GetWidgetListener();
  if (listener) listener->WindowActivated();

  if (isMozWindow) [[self window] setSuppressMakeKeyFront:NO];

  if (mGeckoChild->GetInputContext().IsPasswordEditor()) {
    TextInputHandler::EnableSecureEventInput();
  } else {
    TextInputHandler::EnsureSecureEventInputDisabled();
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)viewsWindowDidResignKey {
  if (!mGeckoChild) return;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  nsIWidgetListener* listener = mGeckoChild->GetWidgetListener();
  if (listener) listener->WindowDeactivated();

  TextInputHandler::EnsureSecureEventInputDisabled();
}

// If the call to removeFromSuperview isn't delayed from nsCocoaWindow::
// TearDownView(), the NSView hierarchy might get changed during calls to
// [ChildView drawRect:], which leads to "beyond bounds" exceptions in
// NSCFArray.  For more info see bmo bug 373122.  Apple's docs claim that
// removeFromSuperviewWithoutNeedingDisplay "can be safely invoked during
// display" (whatever "display" means).  But it's _not_ true that it can be
// safely invoked during calls to [NSView drawRect:].  We use
// removeFromSuperview here because there's no longer any danger of being
// "invoked during display", and because doing do clears up bmo bug 384343.
- (void)delayedTearDown {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  [self removeFromSuperview];
  [self release];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

#pragma mark -

// drag'n'drop stuff
#define kDragServiceContractID "@mozilla.org/widget/dragservice;1"

- (NSDragOperation)dragOperationFromDragAction:(int32_t)aDragAction {
  if (nsIDragService::DRAGDROP_ACTION_LINK & aDragAction)
    return NSDragOperationLink;
  if (nsIDragService::DRAGDROP_ACTION_COPY & aDragAction)
    return NSDragOperationCopy;
  if (nsIDragService::DRAGDROP_ACTION_MOVE & aDragAction)
    return NSDragOperationGeneric;
  return NSDragOperationNone;
}

- (LayoutDeviceIntPoint)convertWindowCoordinates:(NSPoint)aPoint {
  if (!mGeckoChild) {
    return LayoutDeviceIntPoint(0, 0);
  }

  NSPoint localPoint = [self convertPoint:aPoint fromView:nil];
  return mGeckoChild->CocoaPointsToDevPixels(localPoint);
}

- (LayoutDeviceIntPoint)convertWindowCoordinatesRoundDown:(NSPoint)aPoint {
  if (!mGeckoChild) {
    return LayoutDeviceIntPoint(0, 0);
  }

  NSPoint localPoint = [self convertPoint:aPoint fromView:nil];
  return mGeckoChild->CocoaPointsToDevPixelsRoundDown(localPoint);
}

// This is a utility function used by NSView drag event methods
// to send events. It contains all of the logic needed for Gecko
// dragging to work. Returns the appropriate cocoa drag operation code.
- (NSDragOperation)doDragAction:(EventMessage)aMessage sender:(id)aSender {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!mGeckoChild) return NSDragOperationNone;

  MOZ_LOG(sCocoaLog, LogLevel::Info, ("ChildView doDragAction: entered\n"));

  if (!mDragService) {
    CallGetService(kDragServiceContractID, &mDragService);
    NS_ASSERTION(mDragService, "Couldn't get a drag service - big problem!");
    if (!mDragService) return NSDragOperationNone;
  }

  nsCOMPtr<nsIDragSession> dragSession;
  if (aMessage == eDragEnter) {
    nsIWidget* widget = mGeckoChild;
    dragSession = mDragService->StartDragSession(widget);
  } else {
    dragSession = mDragService->GetCurrentSession(mGeckoChild);
  }

  if (dragSession) {
    if (aMessage == eDragOver) {
      // fire the drag event at the source. Just ignore whether it was
      // cancelled or not as there isn't actually a means to stop the drag
      dragSession->FireDragEventAtSource(
          eDrag, nsCocoaUtils::ModifiersForEvent([NSApp currentEvent]));
      dragSession->SetCanDrop(false);
    } else if (aMessage == eDrop) {
      // We make the assumption that the dragOver handlers have correctly set
      // the |canDrop| property of the Drag Session.
      bool canDrop = false;
      if (!NS_SUCCEEDED(dragSession->GetCanDrop(&canDrop)) || !canDrop) {
        [self doDragAction:eDragExit sender:aSender];

        nsCOMPtr<nsINode> sourceNode;
        dragSession->GetSourceNode(getter_AddRefs(sourceNode));
        if (!sourceNode) {
          dragSession->EndDragSession(
              false, nsCocoaUtils::ModifiersForEvent([NSApp currentEvent]));
        }
        return NSDragOperationNone;
      }
    }

    unsigned int modifierFlags = [[NSApp currentEvent] modifierFlags];
    uint32_t action = nsIDragService::DRAGDROP_ACTION_MOVE;
    // force copy = option, alias = cmd-option, default is move
    if (modifierFlags & NSEventModifierFlagOption) {
      if (modifierFlags & NSEventModifierFlagCommand)
        action = nsIDragService::DRAGDROP_ACTION_LINK;
      else
        action = nsIDragService::DRAGDROP_ACTION_COPY;
    }
    dragSession->SetDragAction(action);
  }

  // set up gecko event
  WidgetDragEvent geckoEvent(true, aMessage, mGeckoChild);
  nsCocoaUtils::InitInputEvent(geckoEvent, [NSApp currentEvent]);

  // Use our own coordinates in the gecko event.
  // Convert event from gecko global coords to gecko view coords.
  NSPoint draggingLoc = [aSender draggingLocation];

  geckoEvent.mRefPoint = [self convertWindowCoordinates:draggingLoc];

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  mGeckoChild->DispatchInputEvent(&geckoEvent);
  if (!mGeckoChild) return NSDragOperationNone;

  if (dragSession) {
    switch (aMessage) {
      case eDragEnter:
      case eDragOver: {
        uint32_t dragAction;
        dragSession->GetDragAction(&dragAction);

        // If TakeChildProcessDragAction returns something other than
        // DRAGDROP_ACTION_UNINITIALIZED, it means that the last event was sent
        // to the child process and this event is also being sent to the child
        // process. In this case, use the last event's action instead.
        nsDragSession* ds = static_cast<nsDragSession*>(dragSession.get());
        int32_t childDragAction = ds->TakeChildProcessDragAction();
        if (childDragAction != nsIDragService::DRAGDROP_ACTION_UNINITIALIZED) {
          dragAction = childDragAction;
        }

        return [self dragOperationFromDragAction:dragAction];
      }
      case eDragExit:
      case eDrop: {
        nsCOMPtr<nsINode> sourceNode;
        dragSession->GetSourceNode(getter_AddRefs(sourceNode));
        if (!sourceNode) {
          // We're leaving a window while doing a drag that was
          // initiated in a different app. End the drag session,
          // since we're done with it for now (until the user
          // drags back into mozilla).
          dragSession->EndDragSession(
              false, nsCocoaUtils::ModifiersForEvent([NSApp currentEvent]));
        }
        break;
      }
      default:
        break;
    }
  }

  return NSDragOperationGeneric;

  NS_OBJC_END_TRY_BLOCK_RETURN(NSDragOperationNone);
}

- (void)resetCursorRects {
  [self addCursorRect:self.bounds cursor:MOZDynamicCursor.sharedInstance];
}

// NSDraggingDestination
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  MOZ_LOG(sCocoaLog, LogLevel::Info, ("ChildView draggingEntered: entered\n"));

  // there should never be a globalDragPboard when "draggingEntered:" is
  // called, but just in case we'll take care of it here.
  [globalDragPboard release];

  // Set the global drag pasteboard that will be used for this drag session.
  // This will be set back to nil when the drag session ends (mouse exits
  // the view or a drop happens within the view).
  globalDragPboard = [[sender draggingPasteboard] retain];

  return [self doDragAction:eDragEnter sender:sender];

  NS_OBJC_END_TRY_BLOCK_RETURN(NSDragOperationNone);
}

// NSDraggingDestination
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
  MOZ_LOG(sCocoaLog, LogLevel::Info, ("ChildView draggingUpdated: entered\n"));
  return [self doDragAction:eDragOver sender:sender];
}

// NSDraggingDestination
- (void)draggingExited:(id<NSDraggingInfo>)sender {
  MOZ_LOG(sCocoaLog, LogLevel::Info, ("ChildView draggingExited: entered\n"));

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  [self doDragAction:eDragExit sender:sender];
  NS_IF_RELEASE(mDragService);
}

// NSDraggingDestination
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  BOOL handled = [self doDragAction:eDrop sender:sender] != NSDragOperationNone;
  NS_IF_RELEASE(mDragService);
  return handled;
}

// NSDraggingSource
// This is just implemented so we comply with the NSDraggingSource protocol.
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  return UINT_MAX;
}

// NSDraggingSource
- (BOOL)ignoreModifierKeysForDraggingSession:(NSDraggingSession*)session {
  return YES;
}

// NSDraggingSource
- (void)draggingSession:(NSDraggingSession*)aSession
           endedAtPoint:(NSPoint)aPoint
              operation:(NSDragOperation)aOperation {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

#ifdef NIGHTLY_BUILD
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
#endif

  NSEvent* currentEvent = [NSApp currentEvent];
  gUserCancelledDrag = ([currentEvent type] == NSEventTypeKeyDown &&
                        [currentEvent keyCode] == kVK_Escape);

  if (!mDragService) {
    CallGetService(kDragServiceContractID, &mDragService);
    NS_ASSERTION(mDragService, "Couldn't get a drag service - big problem!");
  }

  nsCOMPtr<nsIDragSession> session =
      mDragService->GetCurrentSession(mGeckoChild);
  if (session) {
    // Set the dragend point from the current mouse location
    // FIXME(emilio): Weird that we wouldn't use aPoint instead? Seems to work
    // locally as well...
    // NSPoint pnt = aPoint;
    NSPoint pnt = [NSEvent mouseLocation];
    NSPoint locationInWindow =
        nsCocoaUtils::ConvertPointFromScreen([self window], pnt);
    FlipCocoaScreenCoordinate(pnt);
    LayoutDeviceIntPoint pt = [self convertWindowCoordinates:locationInWindow];
    session->SetDragEndPoint(pt.x, pt.y);

    // XXX: dropEffect should be updated per |aOperation|.
    // As things stand though, |aOperation| isn't well handled within "our"
    // events, that is, when the drop happens within the window: it is set
    // either to NSDragOperationGeneric or to NSDragOperationNone.
    // For that reason, it's not yet possible to override dropEffect per the
    // given OS value, and it's also unclear what's the correct dropEffect
    // value for NSDragOperationGeneric that is passed by other applications.
    // All that said, NSDragOperationNone is still reliable.
    if (aOperation == NSDragOperationNone) {
      if (RefPtr dataTransfer = session->GetDataTransfer()) {
        dataTransfer->SetDropEffectInt(nsIDragService::DRAGDROP_ACTION_NONE);
      }
    }

    session->EndDragSession(true,
                            nsCocoaUtils::ModifiersForEvent(currentEvent));
  }

  session = nullptr;
  NS_IF_RELEASE(mDragService);

  [globalDragPboard release];
  globalDragPboard = nil;
  [gLastDragMouseDownEvent release];
  gLastDragMouseDownEvent = nil;
  mPerformedDrag = YES;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// NSDraggingSource
- (void)draggingSession:(NSDraggingSession*)aSession
           movedToPoint:(NSPoint)aPoint {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  nsCOMPtr<nsIDragService> dragService = mDragService;
  if (!dragService) {
    dragService = do_GetService(kDragServiceContractID);
  }
  if (dragService) {
    RefPtr<nsIDragSession> dragSession;
    nsIWidget* widget = mGeckoChild;
    dragService->GetCurrentSession(widget, getter_AddRefs(dragSession));
    if (dragSession) {
      MOZ_ASSERT(aSession == static_cast<nsDragSession*>(dragSession.get())
                                 ->GetNSDraggingSession());
      dragSession->DragMoved(aPoint.x, aPoint.y);
    }
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// NSDraggingSource
- (void)draggingSession:(NSDraggingSession*)aSession
       willBeginAtPoint:(NSPoint)aPoint {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  // there should never be a globalDragPboard when "willBeginAtPoint:" is
  // called, but just in case we'll take care of it here.
  [globalDragPboard release];

  // Set the global drag pasteboard that will be used for this drag session.
  // This will be set back to nil when the drag session ends (mouse exits
  // the view or a drop happens within the view).
  globalDragPboard = [[aSession draggingPasteboard] retain];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

static NSURL* GetPasteLocation(NSPasteboard* aPasteboard, bool aUseFallback) {
  // First, try to get the paste location from the low level pasteboard.
  PasteboardRef pboardRef = nullptr;
  PasteboardCreate((CFStringRef)[aPasteboard name], &pboardRef);
  if (!pboardRef) {
    return nullptr;
  }
  PasteboardSynchronize(pboardRef);

  CFURLRef urlRef = nullptr;
  PasteboardCopyPasteLocation(pboardRef, &urlRef);
  CFRelease(pboardRef);
  if (urlRef) {
    return [(NSURL*)urlRef autorelease];
  }

  if (!aUseFallback) {
    return nil;
  }

  // If no paste location was present on the pasteboard, fall back to a temp
  // directory instead.
  return [NSURL
      fileURLWithPath:[NSTemporaryDirectory()
                          stringByAppendingPathComponent:@"mozDraggedFiles"]];
}

// NSPasteboardItemDataProvider
- (void)pasteboard:(NSPasteboard*)aPasteboard
                  item:(NSPasteboardItem*)aItem
    provideDataForType:(NSString*)aType {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

#ifdef NIGHTLY_BUILD
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
#endif

  if (!gDraggedTransferables) {
    return;
  }

  uint32_t count = 0;
  gDraggedTransferables->GetLength(&count);

  for (uint32_t j = 0; j < count; j++) {
    nsCOMPtr<nsITransferable> currentTransferable =
        do_QueryElementAt(gDraggedTransferables, j);
    if (!currentTransferable) {
      return;
    }

    // Transform the transferable to an NSDictionary.
    NSDictionary* pasteboardOutputDict =
        nsClipboard::PasteboardDictFromTransferable(currentTransferable);
    if (!pasteboardOutputDict || ![pasteboardOutputDict objectForKey:aType]) {
      return;
    }

    if ([aType
            isEqualToString:[UTIHelper
                                stringFromPboardType:NSPasteboardTypeString]] ||
        [aType
            isEqualToString:[UTIHelper
                                stringFromPboardType:kPublicUrlPboardType]] ||
        [aType isEqualToString:
                   [UTIHelper stringFromPboardType:kPublicUrlNamePboardType]] ||
        ([aType isEqualToString:[UTIHelper
                                    stringFromPboardType:(NSString*)
                                                             kUTTypeFileURL]] &&
         ![[pasteboardOutputDict valueForKey:aType] isEqualToString:@""])) {
      [aPasteboard setString:[pasteboardOutputDict valueForKey:aType]
                     forType:aType];
    } else if ([aType
                   isEqualToString:[UTIHelper stringFromPboardType:
                                                  (NSString*)kUTTypeFileURL]] ||
               [aType isEqualToString:
                          [UTIHelper
                              stringFromPboardType:
                                  (NSString*)kPasteboardTypeFileURLPromise]]) {
      NSURL* url = GetPasteLocation(
          aPasteboard,
          [aType isEqualToString:[UTIHelper
                                     stringFromPboardType:(NSString*)
                                                              kUTTypeFileURL]]);
      nsCOMPtr<nsILocalFileMac> macLocalFile;
      if (NS_FAILED(NS_NewLocalFileWithCFURL((__bridge CFURLRef)url,
                                             getter_AddRefs(macLocalFile)))) {
        NS_ERROR("failed NS_NewLocalFileWithCFURL");
        continue;
      }

      if (!gDraggedTransferables) {
        continue;
      }

      uint32_t transferableCount;
      nsresult rv = gDraggedTransferables->GetLength(&transferableCount);
      if (NS_FAILED(rv)) {
        continue;
      }

      for (uint32_t i = 0; i < transferableCount; i++) {
        nsCOMPtr<nsITransferable> item =
            do_QueryElementAt(gDraggedTransferables, i);
        if (!item) {
          NS_ERROR("no transferable");
          continue;
        }

        item->SetTransferData(kFilePromiseDirectoryMime, macLocalFile);

        // If the dest filename is empty (e.g. the image URL had no path
        // filename, only a query string), provide a fallback so that
        // the file promise data provider does not fail. The correct
        // extension will be added by ValidateFileNameForSaving using
        // the image's MIME type.
        nsCOMPtr<nsISupports> filenamePrimitive;
        nsresult fnRv = item->GetTransferData(
            kFilePromiseDestFilename, getter_AddRefs(filenamePrimitive));
        if (NS_SUCCEEDED(fnRv)) {
          nsCOMPtr<nsISupportsString> filenameStr =
              do_QueryInterface(filenamePrimitive);
          nsAutoString filename;
          if (filenameStr) {
            filenameStr->GetData(filename);
          }
          if (filename.IsEmpty()) {
            nsCOMPtr<nsISupportsString> fallback =
                do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID);
            if (fallback) {
              fallback->SetData(u"unknown"_ns);
              item->SetTransferData(kFilePromiseDestFilename, fallback);
            }
          }
        }

        // Now request the kFilePromiseMime data, which will invoke the data
        // provider. If successful, the file will have been created.
        nsCOMPtr<nsISupports> fileDataPrimitive;
        if (NS_FAILED(item->GetTransferData(
                kFilePromiseMime, getter_AddRefs(fileDataPrimitive)))) {
          continue;
        }

        if ([aType
                isEqualToString:[UTIHelper
                                    stringFromPboardType:(NSString*)
                                                             kUTTypeFileURL]]) {
          // In case of a file URL we need to populate the pasteboard with the
          // path to the file.
          nsCOMPtr<nsIFile> file = do_QueryInterface(fileDataPrimitive);
          if (!file) {
            continue;
          }
          nsAutoCString finalPath;
          file->GetNativePath(finalPath);
          NSString* filePath =
              [NSString stringWithUTF8String:(const char*)finalPath.get()];
          [aPasteboard
              setString:[[NSURL fileURLWithPath:filePath] absoluteString]
                forType:aType];
        }
      }
    } else if ([aType
                   isEqualToString:[UTIHelper stringFromPboardType:
                                                  kUrlsWithTitlesPboardType]]) {
      [aPasteboard setPropertyList:[pasteboardOutputDict valueForKey:aType]
                           forType:aType];
    } else if ([aType isEqualToString:[UTIHelper stringFromPboardType:
                                                     NSPasteboardTypeHTML]]) {
      [aPasteboard setString:(nsClipboard::WrapHtmlForSystemPasteboard(
                                 [pasteboardOutputDict valueForKey:aType]))
                     forType:aType];
    } else if ([aType isEqualToString:[UTIHelper stringFromPboardType:
                                                     NSPasteboardTypeTIFF]] ||
               [aType isEqualToString:[UTIHelper stringFromPboardType:
                                                     NSPasteboardTypePNG]]) {
      [aPasteboard setData:[pasteboardOutputDict valueForKey:aType]
                   forType:aType];
    }

    [aPasteboard
        setData:[pasteboardOutputDict
                    valueForKey:[UTIHelper stringFromPboardType:
                                               kMozCustomTypesPboardType]]
        forType:[UTIHelper stringFromPboardType:kMozCustomTypesPboardType]];
    [aPasteboard
        writeObjects:[pasteboardOutputDict
                         valueForKey:[UTIHelper stringFromPboardType:
                                                    kMozFileUrlsPboardType]]];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

#pragma mark -

// Support for the "Services" menu. We currently only support sending strings
// and HTML to system services.
// This method can be called on any thread (see bug 1751687). We can only
// usefully handle it on the main thread.
- (id)validRequestorForSendType:(NSString*)sendType
                     returnType:(NSString*)returnType {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!NS_IsMainThread()) {
    // We don't have any thread-safe ways of checking whether we can send
    // or receive content. Just say no. In normal cases, we expect this
    // method to be called on the main thread.
    return [super validRequestorForSendType:sendType returnType:returnType];
  }

  // sendType contains the type of data that the service would like this
  // application to send to it.  sendType is nil if the service is not
  // requesting any data.
  //
  // returnType contains the type of data the service would like to
  // return to this application (e.g., to overwrite the selection).
  // returnType is nil if the service will not return any data.
  //
  // The following condition thus triggers when the service expects a string
  // or HTML from us or no data at all AND when the service will either not
  // send back any data to us or will send a string or HTML back to us.

  id result = nil;

  NSString* stringType =
      [UTIHelper stringFromPboardType:NSPasteboardTypeString];
  NSString* htmlType = [UTIHelper stringFromPboardType:NSPasteboardTypeHTML];
  if ((!sendType || [sendType isEqualToString:stringType] ||
       [sendType isEqualToString:htmlType]) &&
      (!returnType || [returnType isEqualToString:stringType] ||
       [returnType isEqualToString:htmlType])) {
    if (mGeckoChild) {
      // Assume that this object will be able to handle this request.
      result = self;

      // Keep the ChildView alive during this operation.
      nsAutoRetainCocoaObject kungFuDeathGrip(self);

      if (sendType) {
        // Determine if there is a current selection (chrome/content).
        if (!nsClipboard::sSelectionCache) {
          result = nil;
        }
      }

      // Determine if we can paste (if receiving data from the service).
      if (mGeckoChild && returnType) {
        WidgetContentCommandEvent command(
            true, eContentCommandPasteTransferable, mGeckoChild, true);
        command.mTimeStamp =
            nsCocoaUtils::GetEventTimeStamp([[NSApp currentEvent] timestamp]);
        // This might possibly destroy our widget (and null out mGeckoChild).
        mGeckoChild->DispatchWindowEvent(command);
        if (!mGeckoChild || !command.mSucceeded || !command.mIsEnabled)
          result = nil;
      }
    }
  }

  // Give the superclass a chance if this object will not handle this request.
  if (!result)
    result = [super validRequestorForSendType:sendType returnType:returnType];

  return result;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (BOOL)writeSelectionToPasteboard:(NSPasteboard*)pboard types:(NSArray*)types {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  // Make sure that the service will accept strings or HTML.
  if (![types
          containsObject:[UTIHelper stringFromPboardType:NSStringPboardType]] &&
      ![types
          containsObject:[UTIHelper
                             stringFromPboardType:NSPasteboardTypeString]] &&
      ![types containsObject:[UTIHelper
                                 stringFromPboardType:NSPasteboardTypeHTML]]) {
    return NO;
  }

  // Bail out if there is no Gecko object.
  if (!mGeckoChild) return NO;

  // Transform the transferable to an NSDictionary.
  NSDictionary* pasteboardOutputDict = nullptr;

  pasteboardOutputDict =
      nsClipboard::PasteboardDictFromTransferable(nsClipboard::sSelectionCache);

  if (!pasteboardOutputDict) return NO;

  // Declare the pasteboard types.
  unsigned int typeCount = [pasteboardOutputDict count];
  NSMutableArray* declaredTypes = [NSMutableArray arrayWithCapacity:typeCount];
  [declaredTypes addObjectsFromArray:[pasteboardOutputDict allKeys]];
  [pboard declareTypes:declaredTypes owner:nil];

  // Write the data to the pasteboard.
  for (unsigned int i = 0; i < typeCount; i++) {
    NSString* currentKey = [declaredTypes objectAtIndex:i];
    id currentValue = [pasteboardOutputDict valueForKey:currentKey];

    if ([currentKey
            isEqualToString:[UTIHelper
                                stringFromPboardType:NSPasteboardTypeString]] ||
        [currentKey
            isEqualToString:[UTIHelper
                                stringFromPboardType:kPublicUrlPboardType]] ||
        [currentKey isEqualToString:[UTIHelper stringFromPboardType:
                                                   kPublicUrlNamePboardType]]) {
      [pboard setString:currentValue forType:currentKey];
    } else if ([currentKey
                   isEqualToString:
                       [UTIHelper stringFromPboardType:NSPasteboardTypeHTML]]) {
      [pboard setString:(nsClipboard::WrapHtmlForSystemPasteboard(currentValue))
                forType:currentKey];
    } else if ([currentKey
                   isEqualToString:
                       [UTIHelper stringFromPboardType:NSPasteboardTypeTIFF]] ||
               [currentKey
                   isEqualToString:
                       [UTIHelper stringFromPboardType:NSPasteboardTypePNG]]) {
      [pboard setData:currentValue forType:currentKey];
    } else if ([currentKey
                   isEqualToString:
                       [UTIHelper
                           stringFromPboardType:
                               (NSString*)kPasteboardTypeFileURLPromise]] ||
               [currentKey
                   isEqualToString:[UTIHelper stringFromPboardType:
                                                  kUrlsWithTitlesPboardType]]) {
      [pboard setPropertyList:currentValue forType:currentKey];
    }
  }
  return YES;

  NS_OBJC_END_TRY_BLOCK_RETURN(NO);
}

// Called if the service wants us to replace the current selection.
- (BOOL)readSelectionFromPasteboard:(NSPasteboard*)pboard {
  nsresult rv = NS_OK;
  nsCOMPtr<nsITransferable> trans =
      do_CreateInstance("@mozilla.org/widget/transferable;1", &rv);
  if (NS_FAILED(rv)) {
    return NO;
  }

  trans->Init(nullptr);
  trans->AddDataFlavor(kTextMime);
  trans->AddDataFlavor(kHTMLMime);

  // Try to get text/plain data first.
  bool hasTextData = false;
  auto textDataOrError =
      nsClipboard::GetDataFromPasteboard(nsLiteralCString(kTextMime), pboard);
  if (!textDataOrError.isErr()) {
    if (auto data = textDataOrError.inspect()) {
      trans->SetTransferData(kTextMime, data);
      hasTextData = true;
    }
  }

  // If there is no text/plain data, try to get text/html data.
  // XXX: this is legacy behavior, should we alway get text/html data instead?
  if (!hasTextData) {
    auto htmlDataOrError =
        nsClipboard::GetDataFromPasteboard(nsLiteralCString(kHTMLMime), pboard);
    if (!htmlDataOrError.isErr()) {
      if (auto data = htmlDataOrError.inspect()) {
        trans->SetTransferData(kHTMLMime, data);
      }
    }
  }

  NS_ENSURE_TRUE(mGeckoChild, false);

  WidgetContentCommandEvent command(true, eContentCommandPasteTransferable,
                                    mGeckoChild);
  command.mTimeStamp =
      nsCocoaUtils::GetEventTimeStamp([[NSApp currentEvent] timestamp]);
  command.mTransferable = trans;
  mGeckoChild->DispatchWindowEvent(command);

  return command.mSucceeded && command.mIsEnabled;
}

- (void)pressureChangeWithEvent:(NSEvent*)event {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK

  NSInteger stage = [event stage];
  if (mLastPressureStage == 1 && stage == 2) {
    NSUserDefaults* userDefaults = [NSUserDefaults standardUserDefaults];
    if ([userDefaults integerForKey:@"com.apple.trackpad.forceClick"] == 1) {
      // This is no public API to get configuration for current force click.
      // This is filed as radar 29294285.
      [self quickLookWithEvent:event];
    }
  }
  mLastPressureStage = stage;

  NS_OBJC_END_TRY_IGNORE_BLOCK
}

nsresult nsCocoaWindow::GetSelectionAsPlaintext(nsAString& aResult) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!nsClipboard::sSelectionCache) {
    MOZ_ASSERT(aResult.IsEmpty());
    return NS_OK;
  }

  // Get the current chrome or content selection.
  NSDictionary* pasteboardOutputDict =
      nsClipboard::PasteboardDictFromTransferable(nsClipboard::sSelectionCache);
  if (NS_WARN_IF(!pasteboardOutputDict)) {
    return NS_ERROR_FAILURE;
  }

  NSString* currentValue = [pasteboardOutputDict
      objectForKey:[UTIHelper stringFromPboardType:NSPasteboardTypeString]];
  if (!currentValue) {
    return NS_OK;
  }
  const char* textSelection = [currentValue UTF8String];
  aResult = NS_ConvertUTF8toUTF16(textSelection);

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

#ifdef DEBUG
nsresult nsCocoaWindow::SetHiDPIMode(bool aHiDPI) {
  nsCocoaUtils::InvalidateHiDPIState();
  Preferences::SetInt("gfx.hidpi.enabled", aHiDPI ? 1 : 0);
  BackingScaleFactorChanged();
  return NS_OK;
}

nsresult nsCocoaWindow::RestoreHiDPIMode() {
  nsCocoaUtils::InvalidateHiDPIState();
  Preferences::ClearUser("gfx.hidpi.enabled");
  BackingScaleFactorChanged();
  return NS_OK;
}
#endif

#pragma mark -

#ifdef ACCESSIBILITY

/* Every ChildView has a corresponding mozDocAccessible object that is doing all
   the heavy lifting. The topmost ChildView corresponds to a mozRootAccessible
   object.

   All ChildView needs to do is to route all accessibility calls (from the
   NSAccessibility APIs) down to its object, pretending that they are the same.
*/
- (id<mozAccessible>)accessible {
  if (!mGeckoChild) return nil;

  id<mozAccessible> nativeAccessible = nil;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  RefPtr<nsCocoaWindow> geckoChild(mGeckoChild);
  RefPtr<a11y::LocalAccessible> accessible = geckoChild->GetWindowAccessible();
  if (!accessible) return nil;

  accessible->GetNativeInterface((void**)&nativeAccessible);

#  ifdef DEBUG_hakan
  NSAssert(![nativeAccessible isExpired], @"native acc is expired!!!");
#  endif

  return nativeAccessible;
}

/* Implementation of formal mozAccessible formal protocol (enabling mozViews
   to talk to mozAccessible objects in the accessibility module). */

- (BOOL)hasRepresentedView {
  return YES;
}

- (id)representedView {
  return self;
}

- (BOOL)hasMozAccessible {
  return [self accessible] != nil;
}

- (BOOL)isRoot {
  return [[self accessible] isRoot];
}

#  pragma mark -

// general

- (BOOL)isAccessibilityElement {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super isAccessibilityElement];

  return [[self accessible] isAccessibilityElement];
}

- (id)accessibilityHitTest:(NSPoint)point {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super accessibilityHitTest:point];

  return [[self accessible] accessibilityHitTest:point];
}

- (id)accessibilityFocusedUIElement {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super accessibilityFocusedUIElement];

  return [[self accessible] accessibilityFocusedUIElement];
}

// actions

- (NSArray*)accessibilityActionNames {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super accessibilityActionNames];

  return [[self accessible] accessibilityActionNames];
}

- (NSString*)accessibilityActionDescription:(NSString*)action {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super accessibilityActionDescription:action];

  return [[self accessible] accessibilityActionDescription:action];
}

- (void)accessibilityPerformAction:(NSString*)action {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super accessibilityPerformAction:action];

  return [[self accessible] accessibilityPerformAction:action];
}

// attributes

- (NSArray*)accessibilityAttributeNames {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super accessibilityAttributeNames];

  return [[self accessible] accessibilityAttributeNames];
}

- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super accessibilityIsAttributeSettable:attribute];

  return [[self accessible] accessibilityIsAttributeSettable:attribute];
}

- (id)accessibilityAttributeValue:(NSString*)attribute {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super accessibilityAttributeValue:attribute];

  if ([attribute isEqualToString:NSAccessibilityParentAttribute]) {
    // Ensure native accessibles with corresponding mozAccessibles reference
    // their native parent, not their mozAccessible parent.
    return [super accessibilityAttributeValue:attribute];
  }

  id<mozAccessible> accessible = [self accessible];

  return [accessible accessibilityAttributeValue:attribute];

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

#endif /* ACCESSIBILITY */

+ (uint32_t)sUniqueKeyEventId {
  return sUniqueKeyEventId;
}

+ (NSMutableDictionary*)sNativeKeyEventsMap {
  // This dictionary is "leaked".
  MOZ_RUNINIT static NSMutableDictionary* sNativeKeyEventsMap =
      [[NSMutableDictionary alloc] init];
  return sNativeKeyEventsMap;
}

@end

@implementation PixelHostingView
- (id)initWithFrame:(NSRect)aRect {
  self = [super initWithFrame:aRect];
  if (!self) {
    return nil;
  }

  self.wantsLayer = YES;
  self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;

  return self;
}

- (BOOL)isFlipped {
  return YES;
}

- (NSView*)hitTest:(NSPoint)aPoint {
  return nil;
}

- (void)drawRect:(NSRect)aRect {
  NS_WARNING("Unexpected call to drawRect: This view returns YES from "
             "wantsUpdateLayer, so "
             "drawRect should not be called.");
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (void)updateLayer {
  [(ChildView*)[self superview] updateRootCALayer];
}

- (BOOL)wantsBestResolutionOpenGLSurface {
  return nsCocoaUtils::HiDPIEnabled() ? YES : NO;
}

@end

#pragma mark -

void ChildViewMouseTracker::OnDestroyView(ChildView* aView) {
  if (sLastMouseEventView == aView) {
    sLastMouseEventView = nil;
    [sLastMouseMoveEvent release];
    sLastMouseMoveEvent = nil;
  }
}

void ChildViewMouseTracker::OnDestroyWindow(NSWindow* aWindow) {
  if (sWindowUnderMouse == aWindow) {
    sWindowUnderMouse = nil;
  }
}

void ChildViewMouseTracker::MouseEnteredWindow(NSEvent* aEvent) {
  NSWindow* window = aEvent.window;
  if (!window.ignoresMouseEvents) {
    sWindowUnderMouse = window;
    ReEvaluateMouseEnterState(aEvent);
  }
}

void ChildViewMouseTracker::MouseExitedWindow(NSEvent* aEvent) {
  if (sWindowUnderMouse == aEvent.window) {
    sWindowUnderMouse = nil;
    [sLastMouseMoveEvent release];
    sLastMouseMoveEvent = nil;
    ReEvaluateMouseEnterState(aEvent);
  }
}

void ChildViewMouseTracker::NativeMenuOpened() {
  // Send a mouse exit event now.
  // The menu consumes all mouse events while it's open, and we don't want to be
  // stuck thinking the mouse is still hovering our window after the mouse has
  // already moved. This could result in unintended cursor changes or tooltips.
  sWindowUnderMouse = nil;
  ReEvaluateMouseEnterState(nil);
}

void ChildViewMouseTracker::NativeMenuClosed() {
  // If a window was hovered before the menu opened, re-enter that window at the
  // last known mouse position. After -[NSView didCloseMenu:withEvent:] is
  // called, any NSTrackingArea updates that were buffered while the menu was
  // open will be replayed.
  if (sLastMouseMoveEvent) {
    sWindowUnderMouse = sLastMouseMoveEvent.window;
    ReEvaluateMouseEnterState(sLastMouseMoveEvent);
  }
}

void ChildViewMouseTracker::ReEvaluateMouseEnterState(NSEvent* aEvent,
                                                      ChildView* aOldView) {
  ChildView* oldView = aOldView ? aOldView : sLastMouseEventView;
  sLastMouseEventView = ViewForEvent(aEvent);
  if (sLastMouseEventView != oldView) {
    // Send enter and / or exit events.
    WidgetMouseEvent::ExitFrom exitFrom =
        [sLastMouseEventView window] == [oldView window]
            ? WidgetMouseEvent::ePlatformChild
            : WidgetMouseEvent::ePlatformTopLevel;
    [oldView sendMouseEnterOrExitEvent:aEvent enter:NO exitFrom:exitFrom];
    // After the cursor exits the window set it to a visible regular arrow
    // cursor.
    if (exitFrom == WidgetMouseEvent::ePlatformTopLevel) {
      [MOZDynamicCursor.sharedInstance
          setNonCustomCursor:nsIWidget::Cursor{eCursor_standard}];
    }
    [sLastMouseEventView sendMouseEnterOrExitEvent:aEvent
                                             enter:YES
                                          exitFrom:exitFrom];
  }
}

void ChildViewMouseTracker::ResendLastMouseMoveEvent() {
  if (sLastMouseMoveEvent) {
    MouseMoved(sLastMouseMoveEvent);
  }
}

void ChildViewMouseTracker::MouseMoved(NSEvent* aEvent) {
  MouseEnteredWindow(aEvent);
  [sLastMouseEventView handleMouseMoved:aEvent];
  if (sLastMouseMoveEvent != aEvent) {
    [sLastMouseMoveEvent release];
    sLastMouseMoveEvent = [aEvent retain];
  }
}

void ChildViewMouseTracker::MouseScrolled(NSEvent* aEvent) {
  if (!nsCocoaUtils::IsMomentumScrollEvent(aEvent)) {
    // Store the position so we can pin future momentum scroll events.
    sLastScrollEventScreenLocation =
        nsCocoaUtils::ScreenLocationForEvent(aEvent);
  }
}

ChildView* ChildViewMouseTracker::ViewForEvent(NSEvent* aEvent) {
  NSWindow* window = sWindowUnderMouse;
  if (!window) return nil;

  NSPoint windowEventLocation =
      nsCocoaUtils::EventLocationForWindow(aEvent, window);
  NSView* view = [[[window contentView] superview] hitTest:windowEventLocation];

  if (![view isKindOfClass:[ChildView class]]) return nil;

  ChildView* childView = (ChildView*)view;
  // If childView is being destroyed return nil.
  if (![childView widget]) return nil;
  return WindowAcceptsEvent(window, aEvent, childView) ? childView : nil;
}

BOOL ChildViewMouseTracker::WindowAcceptsEvent(NSWindow* aWindow,
                                               NSEvent* aEvent,
                                               ChildView* aView,
                                               BOOL aIsClickThrough) {
  // Right mouse down events may get through to all windows, even to a top level
  // window with an open sheet.
  if (!aWindow || [aEvent type] == NSEventTypeRightMouseDown) return YES;

  id delegate = [aWindow delegate];
  if (!delegate || ![delegate isKindOfClass:[WindowDelegate class]]) return YES;

  nsIWidget* windowWidget = [(WindowDelegate*)delegate geckoWidget];
  if (!windowWidget) return YES;

  NSWindow* topLevelWindow = nil;

  switch (windowWidget->GetWindowType()) {
    case WindowType::Popup:
      // If this is a context menu, it won't have a parent. So we'll always
      // accept mouse move events on context menus even when none of our windows
      // is active, which is the right thing to do.
      // For panels, the parent window is the XUL window that owns the panel.
      return WindowAcceptsEvent([aWindow parentWindow], aEvent, aView,
                                aIsClickThrough);

    case WindowType::TopLevel:
    case WindowType::Dialog:
      if (aWindow.attachedSheet) {
        return NO;
      }

      topLevelWindow = aWindow;
      break;
    default:
      return YES;
  }

  if (!topLevelWindow || ([topLevelWindow isMainWindow] && !aIsClickThrough) ||
      [aEvent type] == NSEventTypeOtherMouseDown ||
      (([aEvent modifierFlags] & NSEventModifierFlagCommand) != 0 &&
       [aEvent type] != NSEventTypeMouseMoved))
    return YES;

  // If we're here then we're dealing with a left click or mouse move on an
  // inactive window or something similar. Ask Gecko what to do.
  return [aView inactiveWindowAcceptsMouseEvent:aEvent];
}

nsCocoaWindow::nsCocoaWindow()
    : mWindow(nil),
      mDelegate(nil),
      mChildView(nil),
      mBackingScaleFactor(0.0),
      mFullscreenTransitionAnimation(nil),
      mShadowStyle(WindowShadow::None),
      mAnimationType(nsIWidget::eGenericWindowAnimation),
      mWindowMadeHere(false),
      mSizeMode(nsSizeMode_Normal),
      mInFullScreenMode(false),
      mInNativeFullScreenMode(false),
      mIgnoreOcclusionCount(0),
      mHasStartedNativeFullscreen(false),
      mWindowAnimationBehavior(NSWindowAnimationBehaviorDefault) {
  // Disable automatic tabbing. We need to do this before we
  // orderFront any of our windows.
  NSWindow.allowsAutomaticWindowTabbing = NO;
}

void nsCocoaWindow::DestroyNativeWindow() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow) {
    return;
  }

  MOZ_ASSERT(mWindowMadeHere,
             "We shouldn't be trying to destroy a window we didn't create.");

  // Clear our class state that is keyed off of mWindow. It's our last
  // chance! This ensures that other nsCocoaWindow instances are not waiting
  // for us to finish a native transition that will have no listener once
  // we clear our delegate.
  EndOurNativeTransition();

  // We are about to destroy mWindow. Before we do that, make sure that we
  // hide the window using the Show() method, because it has several side
  // effects that our parent and listeners might be expecting. If we don't
  // do this now, then these side effects will never execute, though the
  // window will definitely no longer be shown.
  Show(false);

  [mWindow removeTrackingArea];

  [mWindow releaseJSObjects];
  // We want to unhook the delegate here because we don't want events
  // sent to it after this object has been destroyed.
  mWindow.delegate = nil;

  // Closing the window will also release it.
  MOZ_ASSERT(mWindow.releasedWhenClosed);
  [mWindow close];

  mWindow = nil;
  [mDelegate autorelease];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

nsCocoaWindow::~nsCocoaWindow() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  RemoveAllChildren();
  if (mWindow && mWindowMadeHere) {
    CancelAllTransitions();
    DestroyNativeWindow();
  }

  // Our NativeLayerRoot must be empty before it is destructed.
  if (mNativeLayerRoot) {
    mNativeLayerRoot->SetLayers({});
  }

  // An nsCocoaWindow object that was in use can be destroyed without Destroy()
  // ever being called on it.  So we also need to do a quick, safe cleanup
  // here (it's too late to just call Destroy(), which can cause crashes).
  // It's particularly important to make sure widgetDestroyed is called on our
  // mView -- this method NULLs mView's mGeckoChild, and NULL checks on
  // mGeckoChild are used throughout the ChildView class to tell if it's safe
  // to use a ChildView object.
  [mChildView widgetDestroyed];  // Safe if mView is nil.
  ClearParent();
  TearDownView();  // Safe if called twice.
  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Find the screen that overlaps aRect the most,
// if none are found default to the mainScreen.
static NSScreen* FindTargetScreenForRect(const DesktopIntRect& aRect) {
  NSScreen* targetScreen = [NSScreen mainScreen];
  NSEnumerator* screenEnum = [[NSScreen screens] objectEnumerator];
  int largestIntersectArea = 0;
  while (NSScreen* screen = [screenEnum nextObject]) {
    DesktopIntRect screenRect =
        nsCocoaUtils::CocoaRectToGeckoRect([screen visibleFrame]);
    screenRect = screenRect.Intersect(aRect);
    int area = screenRect.width * screenRect.height;
    if (area > largestIntersectArea) {
      largestIntersectArea = area;
      targetScreen = screen;
    }
  }
  return targetScreen;
}

DesktopToLayoutDeviceScale ParentBackingScaleFactor(nsIWidget* aParent) {
  if (aParent) {
    return aParent->GetDesktopToDeviceScale();
  }
  return DesktopToLayoutDeviceScale(1.0);
}

// aRect here is specified in desktop pixels
//
// For child windows aRect.{x,y} are offsets from the origin of the parent
// window and not an absolute position.
nsresult nsCocoaWindow::Create(nsIWidget* aParent, const DesktopIntRect& aRect,
                               const widget::InitData& aInitData) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Because the hidden window is created outside of an event loop,
  // we have to provide an autorelease pool (see bug 559075).
  nsAutoreleasePool localPool;

  // Set defaults which can be overridden from aInitData in BaseCreate
  mWindowType = WindowType::TopLevel;
  mBorderStyle = BorderStyle::Default;

  // Ensure that the toolkit is created.
  nsToolkit::GetToolkit();

  Inherited::BaseCreate(aParent, aInitData);

  mAlwaysOnTop = aInitData.mAlwaysOnTop;
  mIsAlert = aInitData.mIsAlert;

  nsresult rv = CreateNativeWindow(nsCocoaUtils::GeckoRectToCocoaRect(aRect),
                                   mBorderStyle, false, aInitData.mIsPrivate);
  NS_ENSURE_SUCCESS(rv, rv);

  mIsAnimationSuppressed = aInitData.mIsAnimationSuppressed;

  // create our content NSView and hook it up to our parent. Recall that
  // NS_NATIVE_WIDGET is the NSView.
  NSView* contentView = mWindow.contentView;
  mChildView = [[ChildView alloc]
      initWithFrame:mWindow.childViewFrameRectForCurrentBounds
         geckoChild:this];
  mChildView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  mNativeLayerRoot =
      NativeLayerRootCA::CreateForCALayer(mChildView.rootCALayer);
  mNativeLayerRoot->SetBackingScale(BackingScaleFactor());

  // Link mChildView into the native NSView hierarchy only after
  // mNativeLayerRoot is initialized. This resolves bug 1986701.
  [contentView addSubview:mChildView];

  [WindowDataMap.sharedWindowDataMap ensureDataForWindow:mWindow];

  NS_ASSERTION(!mTextInputHandler, "mTextInputHandler has already existed");
  mTextInputHandler = new TextInputHandler(this, mChildView);

  [mWindow makeFirstResponder:mChildView];

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

nsresult nsCocoaWindow::Create(nsIWidget* aParent,
                               const LayoutDeviceIntRect& aRect,
                               const widget::InitData& aInitData) {
  DesktopIntRect desktopRect =
      RoundedToInt(aRect / ParentBackingScaleFactor(aParent));
  return Create(aParent, desktopRect, aInitData);
}

static unsigned int WindowMaskForBorderStyle(BorderStyle aBorderStyle) {
  bool allOrDefault = (aBorderStyle == BorderStyle::All ||
                       aBorderStyle == BorderStyle::Default);

  /* Apple's docs on NSWindow styles say that "a window's style mask should
   * include NSWindowStyleMaskTitled if it includes any of the others [besides
   * NSWindowStyleMaskBorderless]".  This implies that a borderless window
   * shouldn't have any other styles than NSWindowStyleMaskBorderless.
   */
  if (!allOrDefault && !(aBorderStyle & BorderStyle::Title)) {
    if (aBorderStyle & BorderStyle::Minimize) {
      /* It appears that at a minimum, borderless windows can be miniaturizable,
       * effectively contradicting some of Apple's documentation referenced
       * above. One such exception is the screen share indicator, see
       * bug 1742877.
       */
      return NSWindowStyleMaskBorderless | NSWindowStyleMaskMiniaturizable;
    }
    return NSWindowStyleMaskBorderless;
  }

  unsigned int mask = NSWindowStyleMaskTitled;
  if (allOrDefault || aBorderStyle & BorderStyle::Close) {
    mask |= NSWindowStyleMaskClosable;
  }
  if (allOrDefault || aBorderStyle & BorderStyle::Minimize) {
    mask |= NSWindowStyleMaskMiniaturizable;
  }
  if (allOrDefault || aBorderStyle & BorderStyle::ResizeH) {
    mask |= NSWindowStyleMaskResizable;
  }

  return mask;
}

// If aRectIsFrameRect, aRect specifies the frame rect of the new window.
// Otherwise, aRect.x/y specify the position of the window's frame relative to
// the bottom of the menubar and aRect.width/height specify the size of the
// content rect.
nsresult nsCocoaWindow::CreateNativeWindow(const NSRect& aRect,
                                           BorderStyle aBorderStyle,
                                           bool aRectIsFrameRect,
                                           bool aIsPrivateBrowsing) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // We default to NSWindowStyleMaskBorderless, add features if needed.
  unsigned int features = NSWindowStyleMaskBorderless;

  // Configure the window we will create based on the window type.
  switch (mWindowType) {
    case WindowType::Invisible:
      break;
    case WindowType::Popup:
      if (aBorderStyle != BorderStyle::Default &&
          mBorderStyle & BorderStyle::Title) {
        features |= NSWindowStyleMaskTitled;
        if (aBorderStyle & BorderStyle::Close) {
          features |= NSWindowStyleMaskClosable;
        }
      }
      break;
    case WindowType::TopLevel:
    case WindowType::Dialog:
      features = WindowMaskForBorderStyle(aBorderStyle);
      break;
    default:
      NS_ERROR("Unhandled window type!");
      return NS_ERROR_FAILURE;
  }

  NSRect contentRect;

  if (aRectIsFrameRect) {
    contentRect = [NSWindow contentRectForFrameRect:aRect styleMask:features];
  } else {
    /*
     * We pass a content area rect to initialize the native Cocoa window. The
     * content rect we give is the same size as the size we're given by gecko.
     * The origin we're given for non-popup windows is moved down by the height
     * of the menu bar so that an origin of (0,100) from gecko puts the window
     * 100 pixels below the top of the available desktop area. We also move the
     * origin down by the height of a title bar if it exists. This is so the
     * origin that gecko gives us for the top-left of  the window turns out to
     * be the top-left of the window we create. This is how it was done in
     * Carbon. If it ought to be different we'll probably need to look at all
     * the callers.
     *
     * Note: This means that if you put a secondary screen on top of your main
     * screen and open a window in the top screen, it'll be incorrectly shifted
     * down by the height of the menu bar. Same thing would happen in Carbon.
     *
     * Note: If you pass a rect with 0,0 for an origin, the window ends up in a
     * weird place for some reason. This stops that without breaking popups.
     */
    // Compensate for difference between frame and content area height (e.g.
    // title bar).
    NSRect newWindowFrame = [NSWindow frameRectForContentRect:aRect
                                                    styleMask:features];

    contentRect = aRect;
    contentRect.origin.y -= (newWindowFrame.size.height - aRect.size.height);

    if (mWindowType != WindowType::Popup) {
      contentRect.origin.y -= NSApp.mainMenu.menuBarHeight;
    }
  }

  // NSLog(@"Top-level window being created at Cocoa rect: %f, %f, %f, %f\n",
  //       rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);

  Class windowClass = [BaseWindow class];
  if ((mWindowType == WindowType::TopLevel ||
       mWindowType == WindowType::Dialog) &&
      (features & NSWindowStyleMaskTitled)) {
    // If we have a titlebar on a top-level window, we want to be able to
    // control the titlebar color (for unified windows), so use the special
    // ToolbarWindow class. Note that we need to check the window type because
    // we mark sheets as having titlebars.
    windowClass = [ToolbarWindow class];
  } else if (mWindowType == WindowType::Popup) {
    windowClass = [PopupWindow class];
    // If we're a popup window we need to use the PopupWindow class.
  } else if (features == NSWindowStyleMaskBorderless) {
    // If we're a non-popup borderless window we need to use the
    // BorderlessWindow class.
    windowClass = [BorderlessWindow class];
  }

  // Create the window
  mWindow = [[windowClass alloc] initWithContentRect:contentRect
                                           styleMask:features
                                             backing:NSBackingStoreBuffered
                                               defer:YES];

  // Make sure that window titles don't leak to disk in private browsing mode
  // due to macOS' resume feature.
  mWindow.restorable = !aIsPrivateBrowsing;
  if (aIsPrivateBrowsing) {
    [mWindow disableSnapshotRestoration];
  }

  // setup our notification delegate. Note that setDelegate: does NOT retain.
  mDelegate = [[WindowDelegate alloc] initWithGeckoWindow:this];
  mWindow.delegate = mDelegate;

  // Make sure that the content rect we gave has been honored.
  NSRect wantedFrame = [mWindow frameRectForChildViewRect:contentRect];
  if (!NSEqualRects(mWindow.frame, wantedFrame)) {
    // This can happen when the window is not on the primary screen.
    [mWindow setFrame:wantedFrame display:NO];
  }
  UpdateBounds();

  if (mWindowType == WindowType::Invisible) {
    mWindow.level = kCGDesktopWindowLevelKey;
  }

  if (mWindowType == WindowType::Popup) {
    SetPopupWindowLevel();
    mWindow.backgroundColor = NSColor.clearColor;
    mWindow.opaque = NO;

    // Enable NSPopover for panel popup types when preference is enabled
    if ([mWindow isKindOfClass:[PopupWindow class]] && ShouldUseNSPopover()) {
      [(PopupWindow*)mWindow setAllowPopover];
    }

    // When multiple spaces are in use and the browser is assigned to a
    // particular space, override the "Assign To" space and display popups on
    // the active space. Does not work with multiple displays. See
    // NeedsRecreateToReshow() for multi-display with multi-space workaround.
    mWindow.collectionBehavior = mWindow.collectionBehavior |
                                 NSWindowCollectionBehaviorMoveToActiveSpace;
  } else {
    // Non-popup windows are always opaque.
    mWindow.opaque = YES;
  }

  if (mAlwaysOnTop || mIsAlert) {
    mWindow.level = NSFloatingWindowLevel;
    mWindow.collectionBehavior =
        mWindow.collectionBehavior | NSWindowCollectionBehaviorCanJoinAllSpaces;
  }
  mWindow.contentMinSize = NSMakeSize(60, 60);

  // Make the window use CoreAnimation from the start, so that we don't
  // switch from a non-CA window to a CA-window in the middle.
  mWindow.contentView.wantsLayer = YES;

  [mWindow createTrackingArea];

  // Make sure the window starts out not draggable by the background.
  // We will turn it on as necessary.
  mWindow.movableByWindowBackground = NO;

  [WindowDataMap.sharedWindowDataMap ensureDataForWindow:mWindow];
  mWindowMadeHere = true;

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

void nsCocoaWindow::Destroy() {
  if (mOnDestroyCalled) {
    return;
  }
  mOnDestroyCalled = true;

  nsCOMPtr<nsIWidget> kungFuDeathGrip(this);

  // Deal with the possibility that we're being destroyed while running modal.
  if (mModal) {
    SetModal(false);
  }

  // If we don't hide here we run into problems with panels, this is not ideal.
  // (Bug 891424)
  Show(false);

  {
    // Make sure that no composition is in progress while disconnecting
    // ourselves from the view.
    MutexAutoLock lock(mCompositingLock);
    [mChildView widgetDestroyed];
  }

  TearDownView();  // Safe if called twice.
  if (mFullscreenTransitionAnimation) {
    [mFullscreenTransitionAnimation stopAnimation];
    ReleaseFullscreenTransitionAnimation();
  }

  if (mInFullScreenMode && !mInNativeFullScreenMode) {
    // Keep these calls balanced for emulated fullscreen.
    nsCocoaUtils::HideOSChromeOnScreen(false);
  }

  // Destroy the native window here (and not wait for that to happen in our
  // destructor). Otherwise this might not happen for several seconds because
  // at least one object holding a reference to ourselves is usually waiting
  // to be garbage-collected.
  if (mWindow && mWindowMadeHere) {
    CancelAllTransitions();
    DestroyNativeWindow();
  }

  nsIWidget::OnDestroy();
  nsIWidget::Destroy();
}

void* nsCocoaWindow::GetNativeData(uint32_t aDataType) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  void* retVal = nullptr;

  switch (aDataType) {
    // to emulate how windows works, we always have to return a NSView
    // for NS_NATIVE_WIDGET
    case NS_NATIVE_WIDGET:
      retVal = [[mChildView retain] autorelease];
      break;

    case NS_NATIVE_WINDOW:
      retVal = [[mWindow retain] autorelease];
      break;

    case NS_NATIVE_GRAPHIC:
      // There isn't anything that makes sense to return here,
      // and it doesn't matter so just return nullptr.
      NS_ERROR("Requesting NS_NATIVE_GRAPHIC on a top-level window!");
      break;
    case NS_RAW_NATIVE_IME_CONTEXT:
      retVal = GetPseudoIMEContext();
      if (retVal) {
        break;
      }
      retVal = [mChildView inputContext];
      // If input context isn't available on this widget, we should set |this|
      // instead of nullptr since if this returns nullptr, IMEStateManager
      // cannot manage composition with TextComposition instance.  Although,
      // this case shouldn't occur.
      if (NS_WARN_IF(!retVal)) {
        retVal = this;
      }
      break;
    case NS_NATIVE_WINDOW_WEBRTC_DEVICE_ID: {
      retVal = (void*)mWindow.windowNumber;
      break;
    }
  }

  return retVal;

  NS_OBJC_END_TRY_BLOCK_RETURN(nullptr);
}

bool nsCocoaWindow::IsVisible() const {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  return mWindow && mWindow.isVisibleOrBeingShown;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

void nsCocoaWindow::SetModal(bool aModal) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mModal == aModal) {
    return;
  }

  // Unlike many functions here, we explicitly *do not check* for the
  // existence of mWindow. This is to ensure that calls to SetModal have
  // no early exits and always update state. That way, if the calls are
  // balanced, we get expected behavior even if the native window has
  // been destroyed during the modal period. Within this function, all
  // the calls to mWindow will resolve even if mWindow is nil (as is
  // guaranteed by Objective-C). And since those calls are only concerned
  // with changing mWindow appearance/level, it's fine for them to be
  // no-ops if mWindow has already been destroyed.

  // This is used during startup (outside the event loop) when creating
  // the add-ons compatibility checking dialog and the profile manager UI;
  // therefore, it needs to provide an autorelease pool to avoid cocoa
  // objects leaking.
  nsAutoreleasePool localPool;

  mModal = aModal;

  if (aModal) {
    sModalWindowCount++;
  } else {
    MOZ_ASSERT(sModalWindowCount);
    sModalWindowCount--;
  }

  // When a window gets "set modal", make the window(s) that it appears over
  // behave as they should.  We can't rely on native methods to do this, for the
  // following reason:  The OS runs modal non-sheet windows in an event loop
  // (using [NSApplication runModalForWindow:] or similar methods) that's
  // incompatible with the modal event loop in AppWindow::ShowModal() (each of
  // these event loops is "exclusive", and can't run at the same time as other
  // (similar) event loops).
  for (auto* ancestorWidget = mParent; ancestorWidget;
       ancestorWidget = ancestorWidget->GetParent()) {
    auto* ancestor = static_cast<nsCocoaWindow*>(ancestorWidget);
    const bool changed = aModal ? ancestor->mNumModalDescendants++ == 0
                                : --ancestor->mNumModalDescendants == 0;
    NS_ASSERTION(ancestor->mNumModalDescendants >= 0,
                 "Widget hierarchy changed while modal!");
    if (!changed || ancestor->mWindowType == WindowType::Invisible) {
      continue;
    }
    NSWindow* win = ancestor->GetCocoaWindow();
    [[win standardWindowButton:NSWindowCloseButton] setEnabled:!aModal];
    [[win standardWindowButton:NSWindowMiniaturizeButton] setEnabled:!aModal];
    [[win standardWindowButton:NSWindowZoomButton] setEnabled:!aModal];
  }
  if (aModal) {
    mWindow.level = NSModalPanelWindowLevel;
  } else if (mWindowType == WindowType::Popup) {
    SetPopupWindowLevel();
  } else {
    mWindow.level = NSNormalWindowLevel;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

bool nsCocoaWindow::IsRunningAppModal() { return [NSApp _isRunningAppModal]; }

static NSRectEdge AlignmentPositionToNSRectEdge(int8_t aPosition) {
  switch (aPosition) {
    case POPUPPOSITION_BEFORESTART:
    case POPUPPOSITION_BEFOREEND:
      return NSRectEdgeMaxY;
    case POPUPPOSITION_AFTERSTART:
    case POPUPPOSITION_AFTEREND:
      return NSRectEdgeMinY;
    case POPUPPOSITION_STARTBEFORE:
    case POPUPPOSITION_STARTAFTER:
      return NSRectEdgeMaxX;
    case POPUPPOSITION_ENDBEFORE:
    case POPUPPOSITION_ENDAFTER:
      return NSRectEdgeMinX;
    default:
      return NSRectEdgeMinY;
  }
}

static void SyncPopoverBounds(NSPopover* aPopover,
                              nsMenuPopupFrame* aPopupFrame) {
  if (!aPopover || !aPopover.shown || !aPopupFrame) {
    return;
  }
  NSWindow* popoverWindow = aPopover.contentViewController.view.window;
  if (!popoverWindow) {
    return;
  }

  // Synchronize the popup frame's internal bounds with the actual bounds that
  // macOS calculated for the popover.
  NSView* contentView = popoverWindow.contentView;
  NSRect contentFrame = [contentView convertRect:contentView.bounds toView:nil];
  NSRect windowFrame = [popoverWindow convertRectToScreen:contentFrame];

  CGFloat backingScale = popoverWindow.backingScaleFactor;
  mozilla::LayoutDeviceIntRect devPixRect =
      nsCocoaUtils::CocoaRectToGeckoRectDevPix(windowFrame, backingScale);

  nsPresContext* presContext = aPopupFrame->PresContext();
  mozilla::CSSIntPoint cssPos =
      presContext->DevPixelsToIntCSSPixels(devPixRect.TopLeft());

  aPopupFrame->MoveTo(mozilla::CSSPoint(cssPos.x, cssPos.y),
                      /* aUpdateAttrs */ false);
}

// Hide or show this window
void nsCocoaWindow::Show(bool aState) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow) {
    return;
  }

  // Early exit if our current visibility state is already the requested
  // state.
  if (aState == mWindow.isVisibleOrBeingShown) {
    return;
  }

  mWindow.isBeingShown = aState;
  if (aState && !mWasShown) {
    mWasShown = true;
  }

  NSWindow* nativeParentWindow =
      mParent ? (NSWindow*)mParent->GetNativeData(NS_NATIVE_WINDOW) : nil;

  if (aState && !mBounds.IsEmpty()) {
    // If we had set the activationPolicy to accessory, then right now we won't
    // have a dock icon. Make sure that we undo that and show a dock icon now
    // that we're going to show a window.
    if (NSApp.activationPolicy != NSApplicationActivationPolicyRegular) {
      NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
      PR_SetEnv("MOZ_APP_NO_DOCK=");
    }

    // Don't try to show a popup when the parent isn't visible or is minimized.
    if (mWindowType == WindowType::Popup && nativeParentWindow) {
      if (!nativeParentWindow.isVisible || nativeParentWindow.isMiniaturized) {
        return;
      }
    }

    // We're about to show a window. If we are opening the new window while the
    // user is in a fullscreen space, for example because the new window is
    // opened from an existing fullscreen window, then macOS will open the new
    // window in fullscreen, too. For some windows, this is not desirable. We
    // want to prevent it for any popup, alert, or alwaysOnTop windows that
    // aren't already in fullscreen. If the user already got the window into
    // fullscreen somehow, that's fine, but we don't want the initial display to
    // be in fullscreen.
    bool savedValueForSupportsNativeFullscreen = GetSupportsNativeFullscreen();
    if (!mInFullScreenMode &&
        ((mWindowType == WindowType::Popup) || mAlwaysOnTop || mIsAlert)) {
      SetSupportsNativeFullscreen(false);
    }

    if (mWindowType == WindowType::Popup) {
      // For reasons that aren't yet clear, calls to [NSWindow orderFront:] or
      // [NSWindow makeKeyAndOrderFront:] can sometimes trigger "Error (1000)
      // creating CGSWindow", which in turn triggers an internal inconsistency
      // NSException.  These errors shouldn't be fatal.  So we need to wrap
      // calls to ...orderFront: in TRY blocks.  See bmo bug 470864.
      NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;
      mWindow.contentView.needsDisplay = YES;
      if (!nativeParentWindow || mPopupLevel != PopupLevel::Parent) {
        [mWindow orderFront:nil];
      }
      NS_OBJC_END_TRY_IGNORE_BLOCK;
      if (ShouldShowAsNSPopover() && nativeParentWindow) {
        nsMenuPopupFrame* popupFrame = GetPopupFrame();
        NSRectEdge preferredEdge =
            AlignmentPositionToNSRectEdge(popupFrame->GetAlignmentPosition());
        nsRect anchorRectAppUnits = popupFrame->GetUntransformedAnchorRect();
        nsPresContext* pc = popupFrame->PresContext();
        int32_t appUnitsPerDevPixel = pc->AppUnitsPerDevPixel();
        mozilla::DesktopToLayoutDeviceScale desktopToLayoutScale =
            pc->DeviceContext()->GetDesktopToDeviceScale();
        mozilla::DesktopIntRect popupAnchorRectScaled =
            mozilla::DesktopIntRect::RoundOut(
                mozilla::LayoutDeviceRect::FromAppUnits(anchorRectAppUnits,
                                                        appUnitsPerDevPixel) /
                desktopToLayoutScale);
        // Taking the now correctly scaled anchor rect and turning it into a
        // gecko rect this accounts for the y-axis inversion that cocoa needs,
        // as the origin is in the bottom left. This rect is in screen space
        NSRect cocoaScreenRect =
            nsCocoaUtils::GeckoRectToCocoaRect(popupAnchorRectScaled);
        // We take the screen space rect and convert it to window space
        // coordinates, as NSPopover requires the coordinates to be in view
        // space and inside the view. If the coordinates are outside our view,
        // the popover will fail silently
        NSRect windowRect =
            [nativeParentWindow convertRectFromScreen:cocoaScreenRect];
        NSView* parentView = [nativeParentWindow contentView];
        // We take the window space rect and convert it to view space for the
        // specific parent view
        NSRect positioningRect = [parentView convertRect:windowRect
                                                fromView:nil];
        bool shouldHideAnchor =
            popupFrame->PopupElement().GetBoolAttr(nsGkAtoms::hidepopovertail);
        [(PopupWindow*)mWindow showPopoverRelativeToRect:positioningRect
                                                  ofView:parentView
                                           preferredEdge:preferredEdge
                                            hiddenAnchor:shouldHideAnchor];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-method-access"
        SyncPopoverBounds([(PopupWindow*)mWindow popover], popupFrame);
#pragma clang diagnostic pop
        // Exit early here since the popover is now shown.
        return;
      }
      // If our popup window is a non-native context menu, tell the OS (and
      // other programs) that a menu has opened.  This is how the OS knows to
      // close other programs' context menus when ours open.
      if ([mWindow isKindOfClass:[PopupWindow class]] &&
          [(PopupWindow*)mWindow isContextMenu]) {
        [NSDistributedNotificationCenter.defaultCenter
            postNotificationName:
                @"com.apple.HIToolbox.beginMenuTrackingNotification"
                          object:@"org.mozilla.gecko.PopupWindow"];
      }

      // If a parent window was supplied and this is a popup at the parent
      // level, set its child window. This will cause the child window to
      // appear above the parent and move when the parent does.
      if (nativeParentWindow && mPopupLevel == PopupLevel::Parent) {
        [nativeParentWindow addChildWindow:mWindow ordered:NSWindowAbove];
      }
    } else {
      NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;
      if (mWindowType == WindowType::TopLevel &&
          [mWindow respondsToSelector:@selector(setAnimationBehavior:)]) {
        NSWindowAnimationBehavior behavior;
        if (mIsAnimationSuppressed) {
          behavior = NSWindowAnimationBehaviorNone;
        } else {
          switch (mAnimationType) {
            case nsIWidget::eDocumentWindowAnimation:
              behavior = NSWindowAnimationBehaviorDocumentWindow;
              break;
            default:
              MOZ_FALLTHROUGH_ASSERT("unexpected mAnimationType value");
            case nsIWidget::eGenericWindowAnimation:
              behavior = NSWindowAnimationBehaviorDefault;
              break;
          }
        }
        [mWindow setAnimationBehavior:behavior];
        mWindowAnimationBehavior = behavior;
      }

      // We don't want most alwaysontop / alert windows to pull focus when
      // they're opened, as these tend to be for peripheral indicators and
      // displays.
      if ((mAlwaysOnTop && mPiPType != PiPType::DocumentPiP) || mIsAlert) {
        [mWindow orderFront:nil];
      } else {
        [mWindow makeKeyAndOrderFront:nil];
      }
      NS_OBJC_END_TRY_IGNORE_BLOCK;
    }
    SetSupportsNativeFullscreen(savedValueForSupportsNativeFullscreen);

    // If we've previously tried to call MoveToWorkspace but the window wasn't
    // visible, then we saved the call for later. Now is the time to actually
    // do it.
    if (mDeferredWorkspaceID) {
      NS_OBJC_BEGIN_TRY_IGNORE_BLOCK
      MoveVisibleWindowToWorkspace(mDeferredWorkspaceID);
      NS_OBJC_END_TRY_IGNORE_BLOCK
      mDeferredWorkspaceID = 0;
    }
  } else {
    // roll up any popups if a top-level window is going away
    if (IsTopLevelWidget()) {
      RollUpPopups();
    }

    // If the window is a popup window with a parent window we need to
    // unhook it here before ordering it out. When you order out the child
    // of a window it hides the parent window.
    if (mWindowType == WindowType::Popup && nativeParentWindow) {
      [nativeParentWindow removeChildWindow:mWindow];
    }
    // Handle NSPopover hiding or traditional window hiding
    if ([mWindow isKindOfClass:[PopupWindow class]] &&
        [(PopupWindow*)mWindow usePopover]) {
      [(PopupWindow*)mWindow closePopover];
    }
    [mWindow orderOut:nil];
    // If our popup window is a non-native context menu, tell the OS (and
    // other programs) that a menu has closed.
    if ([mWindow isKindOfClass:[PopupWindow class]] &&
        [(PopupWindow*)mWindow isContextMenu]) {
      [NSDistributedNotificationCenter.defaultCenter
          postNotificationName:
              @"com.apple.HIToolbox.endMenuTrackingNotification"
                        object:@"org.mozilla.gecko.PopupWindow"];
    }
  }

  mWindow.isBeingShown = NO;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Work around a problem where with multiple displays and multiple spaces
// enabled, where the browser is assigned to a single display or space, popup
// windows that are reshown after being hidden with [NSWindow orderOut] show on
// the assigned space even when opened from another display. Apply the
// workaround whenever more than one display is enabled.
bool nsCocoaWindow::NeedsRecreateToReshow() {
  // Limit the workaround to popup windows because only they need to override
  // the "Assign To" setting. i.e., to display where the parent window is.
  return mWindowType == WindowType::Popup && mWasShown &&
         NSScreen.screens.count > 1;
}

bool nsCocoaWindow::ShouldUseOffMainThreadCompositing() {
  // We need to enable OMTC in popups which contain remote layer
  // trees, since the remote content won't be rendered at all otherwise.
  if (HasRemoteContent()) {
    return true;
  }

  // Don't use OMTC for popup windows, because we do not want context menus to
  // pay the overhead of starting up a compositor. With the OpenGL compositor,
  // new windows are expensive because of shader re-compilation, and with
  // WebRender, new windows are expensive because they create their own threads
  // and texture caches.
  // Using OMTC with BasicCompositor for context menus would probably be fine
  // but isn't a well-tested configuration.
  if (mWindowType == WindowType::Popup) {
    // Use main-thread BasicLayerManager for drawing menus.
    return false;
  }
  return nsIWidget::ShouldUseOffMainThreadCompositing();
}

bool nsCocoaWindow::ShouldUseNSPopover() const {
  // Use NSPopover for panel popups when the preference is enabled
  // But not for detached popups - they should use traditional window logic
  return mWindowType == WindowType::Popup && mPopupType == PopupType::Panel &&
         mozilla::StaticPrefs::widget_macos_native_popovers();
}

bool nsCocoaWindow::ShouldShowAsNSPopover() const {
  if (!ShouldUseNSPopover()) {
    return false;
  }
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  return [mWindow isKindOfClass:[PopupWindow class]] &&
         [(PopupWindow*)mWindow usePopover] && popupFrame &&
         popupFrame->ShouldFollowAnchor() &&
         !popupFrame->PopupElement().GetBoolAttr(nsGkAtoms::nonnative);
}

TransparencyMode nsCocoaWindow::GetTransparencyMode() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  return mWindow.isOpaque ? TransparencyMode::Opaque
                          : TransparencyMode::Transparent;

  NS_OBJC_END_TRY_BLOCK_RETURN(TransparencyMode::Opaque);
}

// This is called from nsMenuPopupFrame when making a popup transparent.
void nsCocoaWindow::SetTransparencyMode(TransparencyMode aMode) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow) {
    return;
  }

  BOOL isTransparent = aMode == TransparencyMode::Transparent;
  BOOL currentTransparency = !mWindow.isOpaque;
  if (isTransparent == currentTransparency) {
    return;
  }
  mWindow.opaque = !isTransparent;
  mWindow.backgroundColor =
      isTransparent ? NSColor.clearColor : NSColor.whiteColor;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::Enable(bool aState) {}

bool nsCocoaWindow::IsEnabled() const { return true; }

void nsCocoaWindow::ConstrainPosition(DesktopIntPoint& aPoint) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow || ![mWindow screen]) {
    return;
  }

  DesktopIntRect screenRect;

  int32_t width, height;

  NSRect frame = mWindow.frame;

  // zero size rects confuse the screen manager
  width = std::max<int32_t>(frame.size.width, 1);
  height = std::max<int32_t>(frame.size.height, 1);

  nsCOMPtr<nsIScreenManager> screenMgr =
      do_GetService("@mozilla.org/gfx/screenmanager;1");
  if (screenMgr) {
    nsCOMPtr<nsIScreen> screen;
    screenMgr->ScreenForRect(aPoint.x, aPoint.y, width, height,
                             getter_AddRefs(screen));

    if (screen) {
      screenRect = screen->GetRectDisplayPix();
    }
  }

  aPoint = ConstrainPositionToBounds(aPoint, {width, height}, screenRect);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::SetSizeConstraints(const SizeConstraints& aConstraints) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  // On macOS, Cocoa points and desktop pixels are equivalent, so constraints
  // (which are in desktop pixels) can be applied directly to the NSWindow.

  // Popups can be smaller than (32, 32)
  NSRect rect = (mWindowType == WindowType::Popup)
                    ? NSZeroRect
                    : NSMakeRect(0.0, 0.0, 32, 32);
  rect = [mWindow frameRectForChildViewRect:rect];

  SizeConstraints c = aConstraints;
  c.mMinSize.width = std::max(NSToIntRound(rect.size.width), c.mMinSize.width);
  c.mMinSize.height =
      std::max(NSToIntRound(rect.size.height), c.mMinSize.height);

  mWindow.minSize = NSMakeSize(c.mMinSize.width, c.mMinSize.height);
  mWindow.maxSize =
      NSMakeSize(c.mMaxSize.width == NS_MAXSIZE ? FLT_MAX : c.mMaxSize.width,
                 c.mMaxSize.height == NS_MAXSIZE ? FLT_MAX : c.mMaxSize.height);
  nsIWidget::SetSizeConstraints(c);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Coordinates are desktop pixels
void nsCocoaWindow::Move(const DesktopPoint& aPoint) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow) {
    return;
  }

  // The point we have is in Gecko coordinates (origin top-left). Convert
  // it to Cocoa ones (origin bottom-left).
  NSPoint coord = {
      static_cast<float>(aPoint.x),
      static_cast<float>(nsCocoaUtils::FlippedScreenY(NSToIntRound(aPoint.y)))};

  NSRect frame = mWindow.frame;
  if (frame.origin.x != coord.x ||
      frame.origin.y + frame.size.height != coord.y) {
    [mWindow setFrameTopLeftPoint:coord];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::SetSizeMode(nsSizeMode aMode) {
  if (aMode == nsSizeMode_Normal) {
    QueueTransition(TransitionType::Windowed);
  } else if (aMode == nsSizeMode_Minimized) {
    QueueTransition(TransitionType::Miniaturize);
  } else if (aMode == nsSizeMode_Maximized) {
    QueueTransition(TransitionType::Zoom);
  } else if (aMode == nsSizeMode_Fullscreen) {
    MakeFullScreen(true);
  }
}

// The (work)space switching implementation below was inspired by Phoenix:
// https://github.com/kasper/phoenix/tree/d6c877f62b30a060dff119d8416b0934f76af534
// License: MIT.

// Runtime `CGSGetActiveSpace` library function feature detection.
typedef CGSSpaceID (*CGSGetActiveSpaceFunc)(CGSConnection cid);
static CGSGetActiveSpaceFunc GetCGSGetActiveSpaceFunc() {
  static CGSGetActiveSpaceFunc func = nullptr;
  static bool lookedUpFunc = false;
  if (!lookedUpFunc) {
    func = (CGSGetActiveSpaceFunc)dlsym(RTLD_DEFAULT, "CGSGetActiveSpace");
    lookedUpFunc = true;
  }
  return func;
}
// Runtime `CGSCopyManagedDisplaySpaces` library function feature detection.
typedef CFArrayRef (*CGSCopyManagedDisplaySpacesFunc)(CGSConnection cid);
static CGSCopyManagedDisplaySpacesFunc GetCGSCopyManagedDisplaySpacesFunc() {
  static CGSCopyManagedDisplaySpacesFunc func = nullptr;
  static bool lookedUpFunc = false;
  if (!lookedUpFunc) {
    func = (CGSCopyManagedDisplaySpacesFunc)dlsym(
        RTLD_DEFAULT, "CGSCopyManagedDisplaySpaces");
    lookedUpFunc = true;
  }
  return func;
}
// Runtime `CGSCopySpacesForWindows` library function feature detection.
typedef CFArrayRef (*CGSCopySpacesForWindowsFunc)(CGSConnection cid,
                                                  CGSSpaceMask mask,
                                                  CFArrayRef windowIDs);
static CGSCopySpacesForWindowsFunc GetCGSCopySpacesForWindowsFunc() {
  static CGSCopySpacesForWindowsFunc func = nullptr;
  static bool lookedUpFunc = false;
  if (!lookedUpFunc) {
    func = (CGSCopySpacesForWindowsFunc)dlsym(RTLD_DEFAULT,
                                              "CGSCopySpacesForWindows");
    lookedUpFunc = true;
  }
  return func;
}
// Runtime `CGSAddWindowsToSpaces` library function feature detection.
typedef void (*CGSAddWindowsToSpacesFunc)(CGSConnection cid,
                                          CFArrayRef windowIDs,
                                          CFArrayRef spaceIDs);
static CGSAddWindowsToSpacesFunc GetCGSAddWindowsToSpacesFunc() {
  static CGSAddWindowsToSpacesFunc func = nullptr;
  static bool lookedUpFunc = false;
  if (!lookedUpFunc) {
    func =
        (CGSAddWindowsToSpacesFunc)dlsym(RTLD_DEFAULT, "CGSAddWindowsToSpaces");
    lookedUpFunc = true;
  }
  return func;
}
// Runtime `CGSRemoveWindowsFromSpaces` library function feature detection.
typedef void (*CGSRemoveWindowsFromSpacesFunc)(CGSConnection cid,
                                               CFArrayRef windowIDs,
                                               CFArrayRef spaceIDs);
static CGSRemoveWindowsFromSpacesFunc GetCGSRemoveWindowsFromSpacesFunc() {
  static CGSRemoveWindowsFromSpacesFunc func = nullptr;
  static bool lookedUpFunc = false;
  if (!lookedUpFunc) {
    func = (CGSRemoveWindowsFromSpacesFunc)dlsym(RTLD_DEFAULT,
                                                 "CGSRemoveWindowsFromSpaces");
    lookedUpFunc = true;
  }
  return func;
}

void nsCocoaWindow::GetWorkspaceID(nsAString& workspaceID) {
  workspaceID.Truncate();
  int32_t sid = GetWorkspaceID();
  if (sid != 0) {
    workspaceID.AppendInt(sid);
  }
}

int32_t nsCocoaWindow::GetWorkspaceID() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // macOS space IDs start at '1' (default space), so '0' means 'unknown',
  // effectively.
  CGSSpaceID sid = 0;

  CGSCopySpacesForWindowsFunc CopySpacesForWindows =
      GetCGSCopySpacesForWindowsFunc();
  if (!CopySpacesForWindows) {
    return sid;
  }

  CGSConnection cid = _CGSDefaultConnection();
  // Fetch all spaces that this window belongs to (in order).
  NSArray<NSNumber*>* spaceIDs = CFBridgingRelease(CopySpacesForWindows(
      cid, kCGSAllSpacesMask,
      (__bridge CFArrayRef) @[ @([mWindow windowNumber]) ]));
  if ([spaceIDs count]) {
    // When spaces are found, return the first one.
    // We don't support a single window painted across multiple places for now.
    sid = [spaceIDs[0] integerValue];
  } else {
    // Fall back to the workspace that's currently active, which is '1' in the
    // common case.
    CGSGetActiveSpaceFunc GetActiveSpace = GetCGSGetActiveSpaceFunc();
    if (GetActiveSpace) {
      sid = GetActiveSpace(cid);
    }
  }

  return sid;

  NS_OBJC_END_TRY_BLOCK_RETURN(0);
}

void nsCocoaWindow::MoveToWorkspace(const nsAString& workspaceIDStr) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  nsresult rv = NS_OK;
  int32_t workspaceID = workspaceIDStr.ToInteger(&rv);
  if (NS_FAILED(rv)) {
    return;
  }

  // Calling [NSWindow windowNumber] on a window which doesn't have a window
  // device will return -1. This happens when a window was created with
  // defer:YES and has never been visible. During startup, the main app window
  // won't yet have a window device when session restore tries to move it to its
  // workspace, so we defer the move until it's actually shown. That's in the
  // nsCocoaWindow::Show method.
  if (mWindow.isVisible) {
    MoveVisibleWindowToWorkspace(workspaceID);
  } else {
    mDeferredWorkspaceID = workspaceID;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::MoveVisibleWindowToWorkspace(int32_t workspaceID) {
  CGSConnection cid = _CGSDefaultConnection();
  int32_t currentSpace = GetWorkspaceID();
  // If an empty workspace ID is passed in (not valid on macOS), or when the
  // window is already on this workspace, we don't need to do anything.
  if (!workspaceID || workspaceID == currentSpace) {
    return;
  }

  CGSCopyManagedDisplaySpacesFunc CopyManagedDisplaySpaces =
      GetCGSCopyManagedDisplaySpacesFunc();
  CGSAddWindowsToSpacesFunc AddWindowsToSpaces = GetCGSAddWindowsToSpacesFunc();
  CGSRemoveWindowsFromSpacesFunc RemoveWindowsFromSpaces =
      GetCGSRemoveWindowsFromSpacesFunc();
  if (!CopyManagedDisplaySpaces || !AddWindowsToSpaces ||
      !RemoveWindowsFromSpaces) {
    return;
  }

  // Fetch an ordered list of all known spaces.
  NSArray* displaySpacesInfo = CFBridgingRelease(CopyManagedDisplaySpaces(cid));
  // When we found the space we're looking for, we can bail out of the loop
  // early, which this local variable is used for.
  BOOL found = false;
  for (NSDictionary<NSString*, id>* spacesInfo in displaySpacesInfo) {
    NSArray<NSNumber*>* sids =
        [spacesInfo[CGSSpacesKey] valueForKey:CGSSpaceIDKey];
    for (NSNumber* sid in sids) {
      // If we found our space in the list, we're good to go and can jump out of
      // this loop.
      if ((int)[sid integerValue] == workspaceID) {
        found = true;
        break;
      }
    }
    if (found) {
      break;
    }
  }

  // We were unable to find the space to correspond with the workspaceID as
  // requested, so let's bail out.
  if (!found) {
    return;
  }

  // First we add the window to the appropriate space.
  AddWindowsToSpaces(cid, (__bridge CFArrayRef) @[ @([mWindow windowNumber]) ],
                     (__bridge CFArrayRef) @[ @(workspaceID) ]);
  // Then we remove the window from the active space.
  RemoveWindowsFromSpaces(cid,
                          (__bridge CFArrayRef) @[ @([mWindow windowNumber]) ],
                          (__bridge CFArrayRef) @[ @(currentSpace) ]);
}

void nsCocoaWindow::SuppressAnimation(bool aSuppress) {
  if ([mWindow respondsToSelector:@selector(setAnimationBehavior:)]) {
    mWindow.isAnimationSuppressed = aSuppress;
    mWindow.animationBehavior =
        aSuppress ? NSWindowAnimationBehaviorNone : mWindowAnimationBehavior;
  }
}

// This has to preserve the window's frame bounds.
// This method requires (as does the Windows impl.) that you call Resize shortly
// after calling HideWindowChrome. See bug 498835 for fixing this.
void nsCocoaWindow::HideWindowChrome(bool aShouldHide) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow || !mWindowMadeHere ||
      (mWindowType != WindowType::TopLevel &&
       mWindowType != WindowType::Dialog)) {
    return;
  }

  const BOOL isVisible = mWindow.isVisible;

  // Remove child windows.
  NSArray* childWindows = [mWindow childWindows];
  NSEnumerator* enumerator = [childWindows objectEnumerator];
  NSWindow* child = nil;
  while ((child = [enumerator nextObject])) {
    [mWindow removeChildWindow:child];
  }

  // Remove the views in the old window's content view.
  // The NSArray is autoreleased and retains its NSViews.
  NSArray<NSView*>* contentViewContents = [mWindow contentViewContents];
  for (NSView* view in contentViewContents) {
    [view removeFromSuperviewWithoutNeedingDisplay];
  }

  // Save state (like window title).
  NSMutableDictionary* state = [mWindow exportState];

  // Recreate the window with the right border style.
  NSRect frameRect = mWindow.frame;
  BOOL isPrivateWindow = !mWindow.restorable;
  DestroyNativeWindow();
  nsresult rv = CreateNativeWindow(
      frameRect, aShouldHide ? BorderStyle::None : mBorderStyle, true,
      isPrivateWindow);
  NS_ENSURE_SUCCESS_VOID(rv);

  // Re-import state.
  [mWindow importState:state];

  // Add the old content view subviews to the new window's content view.
  for (NSView* view in contentViewContents) {
    [[mWindow contentView] addSubview:view];
  }

  // Reparent child windows.
  enumerator = [childWindows objectEnumerator];
  while ((child = [enumerator nextObject])) {
    [mWindow addChildWindow:child ordered:NSWindowAbove];
  }

  // Show the new window.
  if (isVisible) {
    bool wasAnimationSuppressed = mIsAnimationSuppressed;
    mIsAnimationSuppressed = true;
    Show(true);
    mIsAnimationSuppressed = wasAnimationSuppressed;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

class FullscreenTransitionData : public nsISupports {
 public:
  NS_DECL_ISUPPORTS

  explicit FullscreenTransitionData(NSWindow* aWindow)
      : mTransitionWindow(aWindow) {}

  NSWindow* mTransitionWindow;

 private:
  virtual ~FullscreenTransitionData() { [mTransitionWindow close]; }
};

NS_IMPL_ISUPPORTS0(FullscreenTransitionData)

@interface FullscreenTransitionDelegate : NSObject <NSAnimationDelegate> {
 @public
  nsCocoaWindow* mWindow;
  nsIRunnable* mCallback;
}
@end

@implementation FullscreenTransitionDelegate
- (void)cleanupAndDispatch:(NSAnimation*)animation {
  [animation setDelegate:nil];
  [self autorelease];
  // The caller should have added ref for us.
  NS_DispatchToMainThread(already_AddRefed<nsIRunnable>(mCallback));
}

- (void)animationDidEnd:(NSAnimation*)animation {
  MOZ_ASSERT(animation == mWindow->FullscreenTransitionAnimation(),
             "Should be handling the only animation on the window");
  mWindow->ReleaseFullscreenTransitionAnimation();
  [self cleanupAndDispatch:animation];
}

- (void)animationDidStop:(NSAnimation*)animation {
  [self cleanupAndDispatch:animation];
}
@end

static bool AlwaysUsesNativeFullScreen() {
  return Preferences::GetBool("full-screen-api.macos-native-full-screen",
                              false);
}

/* virtual */ bool nsCocoaWindow::PrepareForFullscreenTransition(
    nsISupports** aData) {
  if (AlwaysUsesNativeFullScreen()) {
    return false;
  }

  // Our fullscreen transition creates a new window occluding this window.
  // That triggers an occlusion event which can cause DOM fullscreen requests
  // to fail due to the context not being focused at the time the focus check
  // is performed in the child process. Until the transition is cleaned up in
  // CleanupFullscreenTransition(), ignore occlusion events for this window.
  // If this method is changed to return false, the transition will not be
  // performed and mIgnoreOcclusionCount should not be incremented.
  MOZ_ASSERT(mIgnoreOcclusionCount >= 0);
  mIgnoreOcclusionCount++;

  nsCOMPtr<nsIScreen> widgetScreen = GetWidgetScreen();
  NSScreen* cocoaScreen = ScreenHelperCocoa::CocoaScreenForScreen(widgetScreen);

  NSWindow* win =
      [[NSWindow alloc] initWithContentRect:cocoaScreen.frame
                                  styleMask:NSWindowStyleMaskBorderless
                                    backing:NSBackingStoreBuffered
                                      defer:YES];
  [win setBackgroundColor:[NSColor blackColor]];
  [win setAlphaValue:0];
  [win setIgnoresMouseEvents:YES];
  [win setLevel:NSScreenSaverWindowLevel];
  [win makeKeyAndOrderFront:nil];

  auto data = new FullscreenTransitionData(win);
  *aData = data;
  NS_ADDREF(data);
  return true;
}

/* virtual */ void nsCocoaWindow::CleanupFullscreenTransition() {
  MOZ_ASSERT(mIgnoreOcclusionCount > 0);
  mIgnoreOcclusionCount--;
}

/* virtual */ void nsCocoaWindow::PerformFullscreenTransition(
    FullscreenTransitionStage aStage, uint16_t aDuration, nsISupports* aData,
    nsIRunnable* aCallback) {
  auto data = static_cast<FullscreenTransitionData*>(aData);
  FullscreenTransitionDelegate* delegate =
      [[FullscreenTransitionDelegate alloc] init];
  delegate->mWindow = this;
  // Storing already_AddRefed directly could cause static checking fail.
  delegate->mCallback = nsCOMPtr<nsIRunnable>(aCallback).forget().take();

  if (mFullscreenTransitionAnimation) {
    [mFullscreenTransitionAnimation stopAnimation];
    ReleaseFullscreenTransitionAnimation();
  }

  NSDictionary* dict = @{
    NSViewAnimationTargetKey : data->mTransitionWindow,
    NSViewAnimationEffectKey : aStage == eBeforeFullscreenToggle
        ? NSViewAnimationFadeInEffect
        : NSViewAnimationFadeOutEffect
  };
  mFullscreenTransitionAnimation =
      [[NSViewAnimation alloc] initWithViewAnimations:@[ dict ]];
  [mFullscreenTransitionAnimation setDelegate:delegate];
  [mFullscreenTransitionAnimation setDuration:aDuration / 1000.0];
  [mFullscreenTransitionAnimation startAnimation];
}

void nsCocoaWindow::CocoaWindowWillEnterFullscreen(bool aFullscreen) {
  MOZ_ASSERT(mUpdateFullscreenOnResize.isNothing());

  mHasStartedNativeFullscreen = true;

  // Snapshot the pre-fullscreen bounds so GetRestoredBounds() can report
  // them while the window is in fullscreen. This fires before macOS starts
  // resizing the window for the fullscreen animation.
  if (aFullscreen && mSizeMode == nsSizeMode_Normal) {
    mRestoredBounds = Some(mBounds);
  }

  // Ensure that we update our fullscreen state as early as possible, when the
  // resize happens.
  mUpdateFullscreenOnResize =
      Some(aFullscreen ? TransitionType::Fullscreen : TransitionType::Windowed);
}

void nsCocoaWindow::CocoaWindowDidEnterFullscreen(bool aFullscreen) {
  EndOurNativeTransition();
  mHasStartedNativeFullscreen = false;
  DispatchOcclusionEvent();

  // The fullscreen window transition leaves the screen-displayed cursor
  // out of sync with our cached state until the next mouse-moved event
  // re-evaluates cursor rects. Re-push the cached cursor now so that
  // e.g. an autohide-driven `cursor: none` stays hidden across the
  // transition (bug 2031413).
  [MOZDynamicCursor.sharedInstance reassertCurrentCursor];

  // Check if aFullscreen matches our expected fullscreen state. It might not if
  // there was a failure somewhere along the way, in which case we'll recover
  // from that.
  bool receivedExpectedFullscreen = false;
  if (mUpdateFullscreenOnResize.isSome()) {
    bool expectingFullscreen =
        (*mUpdateFullscreenOnResize == TransitionType::Fullscreen);
    receivedExpectedFullscreen = (expectingFullscreen == aFullscreen);
  } else {
    receivedExpectedFullscreen = (mInFullScreenMode == aFullscreen);
  }

  TransitionType transition =
      aFullscreen ? TransitionType::Fullscreen : TransitionType::Windowed;
  if (receivedExpectedFullscreen) {
    // Everything is as expected. Update our state if needed.
    HandleUpdateFullscreenOnResize();
  } else {
    // We weren't expecting this fullscreen state. Update our fullscreen state
    // to the new reality.
    UpdateFullscreenState(aFullscreen, true);

    // If we have a current transition, switch it to match what we just did.
    if (mTransitionCurrent.isSome()) {
      mTransitionCurrent = Some(transition);
    }
  }

  // Whether we expected this transition or not, we're ready to finish it.
  FinishCurrentTransitionIfMatching(transition);
}

void nsCocoaWindow::UpdateFullscreenState(bool aFullScreen, bool aNativeMode) {
  bool wasInFullscreen = mInFullScreenMode;
  mInFullScreenMode = aFullScreen;
  if (aNativeMode || mInNativeFullScreenMode) {
    mInNativeFullScreenMode = aFullScreen;
  }

  if (aFullScreen == wasInFullscreen) {
    return;
  }

  [mWindow updateChildViewFrameRect];

  DispatchSizeModeEvent();

  if (mNativeLayerRoot) {
    mNativeLayerRoot->SetWindowIsFullscreen(aFullScreen);
  }
}

nsresult nsCocoaWindow::MakeFullScreen(bool aFullScreen) {
  return DoMakeFullScreen(aFullScreen, AlwaysUsesNativeFullScreen());
}

nsresult nsCocoaWindow::MakeFullScreenWithNativeTransition(bool aFullScreen) {
  return DoMakeFullScreen(aFullScreen, true);
}

nsresult nsCocoaWindow::DoMakeFullScreen(bool aFullScreen,
                                         bool aUseSystemTransition) {
  if (!mWindow) {
    return NS_OK;
  }

  // Figure out what type of transition is being requested.
  TransitionType transition = TransitionType::Windowed;
  if (aFullScreen) {
    // Decide whether to use fullscreen or emulated fullscreen.
    transition =
        (aUseSystemTransition && (mWindow.collectionBehavior &
                                  NSWindowCollectionBehaviorFullScreenPrimary))
            ? TransitionType::Fullscreen
            : TransitionType::EmulatedFullscreen;
  }

  QueueTransition(transition);
  return NS_OK;
}

void nsCocoaWindow::QueueTransition(const TransitionType& aTransition) {
  mTransitionsPending.push(aTransition);
  ProcessTransitions();
}

void nsCocoaWindow::ProcessTransitions() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK

  if (mInProcessTransitions) {
    return;
  }

  mInProcessTransitions = true;

  if (mProcessTransitionsPending) {
    mProcessTransitionsPending->Cancel();
    mProcessTransitionsPending = nullptr;
  }

  // Start a loop that will continue as long as we have transitions to process
  // and we aren't waiting on an asynchronous transition to complete. Any
  // transition that starts something async will `continue` this loop to exit.
  while (!mTransitionsPending.empty() && !IsInTransition()) {
    TransitionType nextTransition = mTransitionsPending.front();

    // We have to check for some incompatible transition states, and if we find
    // one, instead perform an alternative transition and leave the queue
    // untouched. If we add one of these transitions, we set
    // mIsTransitionCurrentAdded because we don't want to confuse listeners who
    // are expecting to receive exactly one event when the requested transition
    // has completed.
    switch (nextTransition) {
      case TransitionType::Fullscreen:
      case TransitionType::EmulatedFullscreen:
      case TransitionType::Windowed:
      case TransitionType::Zoom:
        // These can't handle miniaturized windows, so deminiaturize first.
        if (mWindow.miniaturized) {
          mTransitionCurrent = Some(TransitionType::Deminiaturize);
          mIsTransitionCurrentAdded = true;
        }
        break;
      case TransitionType::Miniaturize:
        // This can't handle fullscreen, so go to windowed first.
        if (mInFullScreenMode) {
          mTransitionCurrent = Some(TransitionType::Windowed);
          mIsTransitionCurrentAdded = true;
        }
        break;
      default:
        break;
    }

    // If mTransitionCurrent is still empty, then we use the nextTransition and
    // pop the queue.
    if (mTransitionCurrent.isNothing()) {
      mTransitionCurrent = Some(nextTransition);
      mTransitionsPending.pop();
    }

    switch (*mTransitionCurrent) {
      case TransitionType::Fullscreen: {
        if (!mInFullScreenMode) {
          // Run a local run loop until it is safe to start a native fullscreen
          // transition.
          NSRunLoop* localRunLoop = [NSRunLoop currentRunLoop];

          // Retain our initial mWindow so it doesn't change under us. We'll
          // release it after finishing the runloop.
          NSWindow* initialWindow = mWindow;
          [initialWindow retain];

          while (!CanStartNativeTransition() &&
                 [localRunLoop runMode:NSDefaultRunLoopMode
                            beforeDate:[NSDate distantFuture]]) {
            // This loop continues to process events until
            // CanStartNativeTransition() returns true.
          }

          // This triggers an async animation, so continue.
          [initialWindow toggleFullScreen:nil];
          [initialWindow release];
          continue;
        }
        break;
      }

      case TransitionType::EmulatedFullscreen: {
        if (!mInFullScreenMode) {
          // Snapshot pre-fullscreen bounds for GetRestoredBounds() before the
          // upcoming resize overwrites mBounds.
          if (mSizeMode == nsSizeMode_Normal) {
            mRestoredBounds = Some(mBounds);
          }
          mSuppressSizeModeEvents = true;
          // The order here matters. When we exit full screen mode, we need to
          // show the Dock first, otherwise the newly-created window won't have
          // its minimize button enabled. See bug 526282.
          nsCocoaUtils::HideOSChromeOnScreen(true);
          nsIWidget::InfallibleMakeFullScreen(true);
          mSuppressSizeModeEvents = false;
          UpdateFullscreenState(true, false);
        }
        break;
      }

      case TransitionType::Windowed: {
        if (mInFullScreenMode) {
          if (mInNativeFullScreenMode) {
            // Run a local run loop until it is safe to start a native
            // fullscreen transition.
            NSRunLoop* localRunLoop = [NSRunLoop currentRunLoop];

            // Retain our initial mWindow so it doesn't change under us. We'll
            // release it after finishing the runloop.
            NSWindow* initialWindow = mWindow;
            [initialWindow retain];

            while (!CanStartNativeTransition() &&
                   [localRunLoop runMode:NSDefaultRunLoopMode
                              beforeDate:[NSDate distantFuture]]) {
              // This loop continues to process events until
              // CanStartNativeTransition() returns true.
            }

            // This triggers an async animation, so continue.
            [initialWindow toggleFullScreen:nil];
            [initialWindow release];
            continue;
          } else {
            mSuppressSizeModeEvents = true;
            // The order here matters. When we exit full screen mode, we need to
            // show the Dock first, otherwise the newly-created window won't
            // have its minimize button enabled. See bug 526282.
            nsCocoaUtils::HideOSChromeOnScreen(false);
            nsIWidget::InfallibleMakeFullScreen(false);
            mSuppressSizeModeEvents = false;
            UpdateFullscreenState(false, false);
          }
        } else if (mWindow.zoomed) {
          [mWindow zoom:nil];

          // Check if we're still zoomed. If we are, we need to do *something*
          // to make the window smaller than the zoom size so Cocoa will treat
          // us as being out of the zoomed state. Otherwise, we could stay
          // zoomed and never be able to be "normal" from calls to SetSizeMode.
          if (mWindow.zoomed) {
            NSRect maximumFrame = mWindow.frame;
            const CGFloat INSET_OUT_OF_ZOOM = 20.0f;
            [mWindow setFrame:NSInsetRect(maximumFrame, INSET_OUT_OF_ZOOM,
                                          INSET_OUT_OF_ZOOM)
                      display:YES];
            MOZ_ASSERT(
                !mWindow.zoomed,
                "We should be able to unzoom by shrinking the frame a bit.");
          }
        }
        break;
      }

      case TransitionType::Miniaturize:
        if (!mWindow.miniaturized) {
          // Snapshot pre-minimize bounds for GetRestoredBounds().
          if (mSizeMode == nsSizeMode_Normal) {
            mRestoredBounds = Some(mBounds);
          }
          // This triggers an async animation, so continue.
          [mWindow miniaturize:nil];
          continue;
        }
        break;

      case TransitionType::Deminiaturize:
        if (mWindow.miniaturized) {
          // This triggers an async animation, so continue.
          [mWindow deminiaturize:nil];
          continue;
        }
        break;

      case TransitionType::Zoom:
        if (!mWindow.zoomed) {
          // Snapshot pre-zoom bounds for GetRestoredBounds() before the
          // zoom resizes the window to fill the screen.
          if (mSizeMode == nsSizeMode_Normal) {
            mRestoredBounds = Some(mBounds);
          }
          [mWindow zoom:nil];
        }
        break;

      default:
        break;
    }

    mTransitionCurrent.reset();
    mIsTransitionCurrentAdded = false;
  }

  mInProcessTransitions = false;

  // When we finish processing transitions, dispatch a size mode event to cover
  // the cases where an inserted transition suppressed one, and the original
  // transition never sent one because it detected it was at the desired state
  // when it ran. If we've already sent a size mode event, then this will be a
  // no-op.
  if (!IsInTransition()) {
    DispatchSizeModeEvent();
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::CancelAllTransitions() {
  // Clear our current and pending transitions. This simplifies our
  // reasoning about what happens next, and ensures that whatever is
  // currently happening won't trigger another call to
  // ProcessTransitions().
  mTransitionCurrent.reset();
  mIsTransitionCurrentAdded = false;
  if (mProcessTransitionsPending) {
    mProcessTransitionsPending->Cancel();
    mProcessTransitionsPending = nullptr;
  }
  std::queue<TransitionType>().swap(mTransitionsPending);
}

void nsCocoaWindow::FinishCurrentTransitionIfMatching(
    const TransitionType& aTransition) {
  // We've just finished some transition activity, and we're not sure whether it
  // was triggered programmatically, or by the user. If it matches our current
  // transition, then assume it was triggered programmatically and we can clean
  // up that transition and start processing transitions again.

  // Whether programmatic or user-initiated, we send out a size mode event.
  DispatchSizeModeEvent();

  if (mTransitionCurrent.isSome() && (*mTransitionCurrent == aTransition)) {
    // This matches our current transition, so do the safe parts of transition
    // cleanup.
    mTransitionCurrent.reset();
    mIsTransitionCurrentAdded = false;

    // Since this function is called from nsWindowDelegate transition callbacks,
    // we want to make sure those callbacks are all the way done before we
    // continue processing more transitions. To accomplish this, we dispatch
    // ProcessTransitions on the next event loop. Doing this will ensure that
    // any async native transition methods we call (like toggleFullScreen) will
    // succeed.
    if (!mTransitionsPending.empty() && !mProcessTransitionsPending) {
      mProcessTransitionsPending = NS_NewCancelableRunnableFunction(
          "ProcessTransitionsPending",
          [self = RefPtr{this}] { self->ProcessTransitions(); });
      NS_DispatchToCurrentThread(mProcessTransitionsPending);
    }
  }
}

bool nsCocoaWindow::HandleUpdateFullscreenOnResize() {
  if (mUpdateFullscreenOnResize.isNothing()) {
    return false;
  }

  bool toFullscreen =
      (*mUpdateFullscreenOnResize == TransitionType::Fullscreen);
  mUpdateFullscreenOnResize.reset();
  UpdateFullscreenState(toFullscreen, true);

  return true;
}

/* static */ nsCocoaWindow* nsCocoaWindow::sWindowInNativeTransition(nullptr);

bool nsCocoaWindow::CanStartNativeTransition() {
  if (sWindowInNativeTransition == nullptr) {
    // Claim it and return true, indicating that the caller has permission to
    // start the native fullscreen transition.
    sWindowInNativeTransition = this;
    return true;
  }
  return false;
}

void nsCocoaWindow::EndOurNativeTransition() {
  if (sWindowInNativeTransition == this) {
    sWindowInNativeTransition = nullptr;
  }
}

// Coordinates are desktop pixels
void nsCocoaWindow::DoResize(double aX, double aY, double aWidth,
                             double aHeight, bool aRepaint,
                             bool aConstrainToCurrentScreen) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow || mInResize) {
    return;
  }

  // We are able to resize a window outside of any aspect ratio contraints
  // applied to it, but in order to "update" the aspect ratio contraint to the
  // new window dimensions, we must re-lock the aspect ratio.
  auto relockAspectRatio = MakeScopeExit([&]() {
    if (mAspectRatioLocked) {
      LockAspectRatio(true);
    }
  });

  AutoRestore<bool> reentrantResizeGuard(mInResize);
  mInResize = true;

  // mSizeConstraints is in desktop pixels, matching aWidth/aHeight.
  int32_t width = NSToIntRound(aWidth);
  int32_t height = NSToIntRound(aHeight);

  width = std::max(mSizeConstraints.mMinSize.width,
                   std::min(mSizeConstraints.mMaxSize.width, width));
  height = std::max(mSizeConstraints.mMinSize.height,
                    std::min(mSizeConstraints.mMaxSize.height, height));

  DesktopIntRect newBounds(NSToIntRound(aX), NSToIntRound(aY), width, height);

  // convert requested bounds into Cocoa coordinate system
  NSRect newFrame = nsCocoaUtils::GeckoRectToCocoaRect(newBounds);

  NSRect frame = mWindow.frame;
  BOOL isMoving = newFrame.origin.x != frame.origin.x ||
                  newFrame.origin.y != frame.origin.y;
  BOOL isResizing = newFrame.size.width != frame.size.width ||
                    newFrame.size.height != frame.size.height;

  if (!isMoving && !isResizing) {
    return;
  }

  // We ignore aRepaint -- we have to call display:YES, otherwise the
  // title bar doesn't immediately get repainted and is displayed in
  // the wrong place, leading to a visual jump.
  [mWindow setFrame:newFrame display:YES];
  if (ShouldUseNSPopover() && [(PopupWindow*)mWindow usePopover]) {
    [(PopupWindow*)mWindow updatePopoverContent];
    // A popover won't resize by setting the frame
    // as it's size is calculated based on the content size
    // Therefore the content size has to be changed as well
    NSSize contentSize = NSMakeSize(aWidth, aHeight);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-method-access"
    [[(PopupWindow*)mWindow popover] setContentSize:contentSize];
    SyncPopoverBounds([(PopupWindow*)mWindow popover], GetPopupFrame());
#pragma clang diagnostic pop
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::Resize(const DesktopRect& aRect, bool aRepaint) {
  DoResize(aRect.x, aRect.y, aRect.width, aRect.height, aRepaint, false);
}

// Coordinates are desktop pixels
void nsCocoaWindow::Resize(const DesktopSize& aSize, bool aRepaint) {
  double invScale = 1.0 / BackingScaleFactor();
  DoResize(mBounds.x * invScale, mBounds.y * invScale, aSize.width,
           aSize.height, aRepaint, true);
}

// Return the area that the Gecko ChildView in our window should cover, as an
// NSRect in screen coordinates (with 0,0 being the bottom left corner of the
// primary screen).
NSRect nsCocoaWindow::GetClientCocoaRect() {
  if (!mWindow) {
    return NSZeroRect;
  }
  return [mWindow childViewRectForFrameRect:mWindow.frame];
}

LayoutDeviceIntRect nsCocoaWindow::GetClientBounds() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  CGFloat scaleFactor = BackingScaleFactor();
  return nsCocoaUtils::CocoaRectToGeckoRectDevPix(GetClientCocoaRect(),
                                                  scaleFactor);

  NS_OBJC_END_TRY_BLOCK_RETURN(LayoutDeviceIntRect(0, 0, 0, 0));
}

void nsCocoaWindow::UpdateBounds() {
  NSRect frame = mWindow ? mWindow.frame : NSZeroRect;
  mBounds =
      nsCocoaUtils::CocoaRectToGeckoRectDevPix(frame, BackingScaleFactor());
}

LayoutDeviceIntRect nsCocoaWindow::GetScreenBounds() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

#ifdef DEBUG
  LayoutDeviceIntRect r = nsCocoaUtils::CocoaRectToGeckoRectDevPix(
      mWindow.frame, BackingScaleFactor());
  NS_ASSERTION(mWindow && mBounds == r, "mBounds out of sync!");
#endif

  return mBounds;

  NS_OBJC_END_TRY_BLOCK_RETURN(LayoutDeviceIntRect(0, 0, 0, 0));
}

nsresult nsCocoaWindow::GetRestoredBounds(LayoutDeviceIntRect& aRect) {
  if (SizeMode() == nsSizeMode_Normal) {
    aRect = GetScreenBounds();
    return NS_OK;
  }
  if (mRestoredBounds.isSome()) {
    aRect = *mRestoredBounds;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

double nsCocoaWindow::GetDefaultScaleInternal() { return BackingScaleFactor(); }

CGFloat nsCocoaWindow::ComputeBackingScaleFactor() const {
  if (nsIWidget* parent = GetParent()) {
    return parent->GetDesktopToDeviceScale().scale;
  }
  NSRect frame = mWindow.frame;
  if (frame.size.width > 0 && frame.size.height > 0) {
    return nsCocoaUtils::GetBackingScaleFactor(mWindow);
  }
  // For windows with zero width or height, the backingScaleFactor method
  // is broken - it will always return 2 on a retina macbook, even when
  // the window position implies it's on a non-hidpi external display
  // (to the extent that a zero-area window can be said to be "on" a
  // display at all!)
  // And to make matters worse, Cocoa even fires a
  // windowDidChangeBackingProperties notification with the
  // NSBackingPropertyOldScaleFactorKey key when a window on an
  // external display is resized to/from zero height, even though it hasn't
  // really changed screens.
  //
  // This causes us to handle popup window sizing incorrectly when the
  // popup is resized to zero height (bug 820327) - nsXULPopupManager
  // becomes (incorrectly) convinced the popup has been explicitly forced
  // to a non-default size and needs to have size attributes attached.
  //
  // Workaround: instead of asking the window, we'll find the screen it is on
  // and ask that for *its* backing scale factor.
  //
  // (See bug 853252 and additional comments in windowDidChangeScreen: below
  // for further complications this causes.)
  // First, expand the rect so that it actually has a measurable area,
  // for FindTargetScreenForRect to use.
  if (frame.size.width == 0) {
    frame.size.width = 1;
  }
  if (frame.size.height == 0) {
    frame.size.height = 1;
  }
  // Then identify the screen it belongs to, and return its scale factor.
  NSScreen* screen =
      FindTargetScreenForRect(nsCocoaUtils::CocoaRectToGeckoRect(frame));
  return nsCocoaUtils::GetBackingScaleFactor(screen);
}

CGFloat nsCocoaWindow::BackingScaleFactor() const {
  if (mBackingScaleFactor > 0.0) {
    return mBackingScaleFactor;
  }
  if (!mWindow) {
    return 1.0;
  }
  mBackingScaleFactor = ComputeBackingScaleFactor();
  return mBackingScaleFactor;
}

void nsCocoaWindow::BackingScaleFactorChanged() {
  CGFloat newScale = ComputeBackingScaleFactor();

  // Ignore notification if it hasn't really changed
  if (BackingScaleFactor() == newScale) {
    return;
  }

  UpdateBounds();

  SuspendAsyncCATransactions();
  mBackingScaleFactor = newScale;
  if (mNativeLayerRoot) {
    mNativeLayerRoot->SetBackingScale(newScale);
  }

  if (mCompositorWidgetDelegate) {
    auto deviceIntRect = GetClientBounds();
    mCompositorWidgetDelegate->NotifyClientSizeChanged(deviceIntRect.Size());
  }

  NotifyAPZOfDPIChange();
  if (mWidgetListener) {
    if (PresShell* presShell = mWidgetListener->GetPresShell()) {
      presShell->BackingScaleFactorChanged();
    }
  }
}

int32_t nsCocoaWindow::RoundsWidgetCoordinatesTo() {
  if (BackingScaleFactor() == 2.0) {
    return 2;
  }
  return 1;
}

nsresult nsCocoaWindow::SetTitle(const nsAString& aTitle) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!mWindow) {
    return NS_OK;
  }

  const nsString& strTitle = PromiseFlatString(aTitle);
  const unichar* uniTitle = reinterpret_cast<const unichar*>(strTitle.get());
  NSString* title = [NSString stringWithCharacters:uniTitle
                                            length:strTitle.Length()];
  if (mWindow.drawsContentsIntoWindowFrame) {
    // Don't cause invalidations when the title isn't displayed.
    [mWindow disableSetNeedsDisplay];
    [mWindow setTitle:title];
    [mWindow enableSetNeedsDisplay];
  } else {
    [mWindow setTitle:title];
  }

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

// Pass notification of some drag event to Gecko
//
// The drag manager has let us know that something related to a drag has
// occurred in this window. It could be any number of things, ranging from
// a drop, to a drag enter/leave, or a drag over event. The actual event
// is passed in |aMessage| and is passed along to our event handler so Gecko
// knows about it.
bool nsCocoaWindow::DragEvent(unsigned int aMessage,
                              mozilla::gfx::Point aMouseGlobal,
                              UInt16 aKeyModifiers) {
  return false;
}

// Invokes callback and ProcessEvent methods on Event Listener object
nsEventStatus nsCocoaWindow::DispatchEvent(WidgetGUIEvent* event) {
  RefPtr kungFuDeathGrip{this};
  if (event->mFlags.mIsSynthesizedForTests) {
    if (WidgetKeyboardEvent* keyEvent = event->AsKeyboardEvent()) {
      nsresult rv = mTextInputHandler->AttachNativeKeyEvent(*keyEvent);
      if (NS_FAILED(rv)) {
        return nsEventStatus_eIgnore;
      }
    }
  }
  return nsIWidget::DispatchEvent(event);
}

// aFullScreen should be the window's mInFullScreenMode. We don't have access to
// that from here, so we need to pass it in. mInFullScreenMode should be the
// canonical indicator that a window is currently full screen and it makes sense
// to keep all sizemode logic here.
static nsSizeMode GetWindowSizeMode(NSWindow* aWindow, bool aFullScreen) {
  if (aFullScreen) {
    return nsSizeMode_Fullscreen;
  }
  if (aWindow.isMiniaturized) {
    return nsSizeMode_Minimized;
  }
  if ((aWindow.styleMask & NSWindowStyleMaskResizable) && aWindow.isZoomed) {
    return nsSizeMode_Maximized;
  }
  return nsSizeMode_Normal;
}

void nsCocoaWindow::ReportMoveEvent() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  // Prevent recursion, which can become infinite (see bug 708278).  This
  // can happen when the call to [NSWindow setFrameTopLeftPoint:] in
  // nsCocoaWindow::Move() triggers an immediate NSWindowDidMove notification
  // (and a call to [WindowDelegate windowDidMove:]).
  if (mInReportMoveEvent) {
    return;
  }
  mInReportMoveEvent = true;

  UpdateBounds();

  // The zoomed state can change when we're moving, in which case we need to
  // update our internal mSizeMode. This can happen either if we're maximized
  // and then moved, or if we're not maximized and moved back to zoomed state.
  if (mWindow && (mSizeMode == nsSizeMode_Maximized) ^ mWindow.isZoomed) {
    DispatchSizeModeEvent();
  }

  // Dispatch the move event to Gecko, if we're visible.
  if (IsVisible()) {
    NotifyWindowMoved(mBounds.TopLeft());
  }

  mInReportMoveEvent = false;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::DispatchSizeModeEvent() {
  if (!mWindow) {
    return;
  }

  if (mSuppressSizeModeEvents || mIsTransitionCurrentAdded) {
    return;
  }

  nsSizeMode newMode = GetWindowSizeMode(mWindow, mInFullScreenMode);
  if (mSizeMode == newMode) {
    return;
  }

  mSizeMode = newMode;
  if (mWidgetListener) {
    mWidgetListener->SizeModeChanged(newMode);
  }
}

void nsCocoaWindow::DispatchOcclusionEvent() {
  if (!mWindow) {
    return;
  }

  // Our new occlusion state is true if the window is not visible.
  bool newOcclusionState =
      !(mHasStartedNativeFullscreen ||
        ([mWindow occlusionState] & NSWindowOcclusionStateVisible));

  // Don't dispatch if the new occlustion state is the same as the current
  // state.
  if (mIsFullyOccluded == newOcclusionState) {
    return;
  }

  MOZ_ASSERT(mIgnoreOcclusionCount >= 0);
  if (newOcclusionState && mIgnoreOcclusionCount > 0) {
    return;
  }

  mIsFullyOccluded = newOcclusionState;
  if (mWidgetListener) {
    mWidgetListener->OcclusionStateChanged(mIsFullyOccluded);
  }
}

void nsCocoaWindow::ReportSizeEvent() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  UpdateBounds();
  LayoutDeviceIntRect innerBounds = GetClientBounds();
  if (mWidgetListener) {
    mWidgetListener->WindowResized(this, innerBounds.Size());
  }
  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->WindowResized(this, innerBounds.Size());
  }
  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::SetMenuBar(RefPtr<nsMenuBarX>&& aMenuBar) {
  if (!mWindow) {
    mMenuBar = nullptr;
    return;
  }
  mMenuBar = std::move(aMenuBar);

  // Only paint for active windows, or paint the hidden window menu bar if no
  // other menu bar has been painted yet so that some reasonable menu bar is
  // displayed when the app starts up.
  if (mMenuBar && ((!gSomeMenuBarPainted &&
                    nsMenuUtilsX::GetHiddenWindowMenuBar() == mMenuBar) ||
                   mWindow.isMainWindow)) {
    // We do an async paint in order to prevent crashes when macOS is actively
    // enumerating the menu items in `NSApp.mainMenu`.
    mMenuBar->PaintAsyncIfNeeded();
  }
}

void nsCocoaWindow::SetFocus(Raise aRaise,
                             mozilla::dom::CallerType aCallerType) {
  if (!mWindow) return;

  if (aRaise == Raise::Yes && (mWindow.isVisible || mWindow.isMiniaturized)) {
    if (mWindow.isMiniaturized) {
      [mWindow deminiaturize:nil];
    }
    [mWindow makeKeyAndOrderFront:nil];
  }
}

LayoutDeviceIntPoint nsCocoaWindow::WidgetToScreenOffset() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  return nsCocoaUtils::CocoaRectToGeckoRectDevPix(GetClientCocoaRect(),
                                                  BackingScaleFactor())
      .TopLeft();

  NS_OBJC_END_TRY_BLOCK_RETURN(LayoutDeviceIntPoint(0, 0));
}

LayoutDeviceIntPoint nsCocoaWindow::GetClientOffset() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  LayoutDeviceIntRect clientRect = GetClientBounds();

  return clientRect.TopLeft() - mBounds.TopLeft();

  NS_OBJC_END_TRY_BLOCK_RETURN(LayoutDeviceIntPoint(0, 0));
}

LayoutDeviceIntMargin nsCocoaWindow::NormalSizeModeClientToWindowMargin() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!mWindow || mWindow.drawsContentsIntoWindowFrame ||
      mWindowType == WindowType::Popup) {
    return {};
  }

  NSRect clientNSRect = mWindow.contentLayoutRect;
  NSRect frameNSRect = [mWindow frameRectForChildViewRect:clientNSRect];

  CGFloat backingScale = BackingScaleFactor();
  const auto clientRect =
      nsCocoaUtils::CocoaRectToGeckoRectDevPix(clientNSRect, backingScale);
  const auto frameRect =
      nsCocoaUtils::CocoaRectToGeckoRectDevPix(frameNSRect, backingScale);

  return frameRect - clientRect;

  NS_OBJC_END_TRY_BLOCK_RETURN({});
}

nsMenuBarX* nsCocoaWindow::GetMenuBar() { return mMenuBar; }

void nsCocoaWindow::CaptureRollupEvents(bool aDoCapture) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (aDoCapture) {
    if (!NSApp.isActive) {
      // We need to capture mouse event if we aren't
      // the active application. We only set this up when needed
      // because they cause spurious mouse event after crash
      // and gdb sessions. See bug 699538.
      nsToolkit::GetToolkit()->MonitorAllProcessMouseEvents();
    }

    // Sometimes more than one popup window can be visible at the same time
    // (e.g. nested non-native context menus, or the test case (attachment
    // 276885) for bmo bug 392389, which displays a non-native combo-box in a
    // non-native popup window).  In these cases the "active" popup window
    // should be the topmost -- the (nested) context menu the mouse is currently
    // over, or the combo-box's drop-down list (when it's displayed).  But
    // (among windows that have the same "level") macOS makes topmost the window
    // that last received a mouse-down event, which may be incorrect (in the
    // combo-box case, it makes topmost the window containing the combo-box).
    // So here we fiddle with a non-native popup window's level to make sure the
    // "active" one is always above any other non-native popup windows that
    // may be visible.
    if (mWindowType == WindowType::Popup) {
      SetPopupWindowLevel();
    }
  } else {
    nsToolkit::GetToolkit()->StopMonitoringAllProcessMouseEvents();

    // XXXndeakin this doesn't make sense.
    // Why is the new window assumed to be a modal panel?
    if (mWindow && mWindowType == WindowType::Popup) {
      mWindow.level = NSModalPanelWindowLevel;
    }
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

nsresult nsCocoaWindow::GetAttention(int32_t aCycleCount) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  [NSApp requestUserAttention:NSInformationalRequest];
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

bool nsCocoaWindow::HasPendingInputEvent() { return DoHasPendingInputEvent(); }

void nsCocoaWindow::SetWindowShadowStyle(WindowShadow aStyle) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mShadowStyle == aStyle) {
    return;
  }

  mShadowStyle = aStyle;

  if (!mWindow || mWindowType != WindowType::Popup) {
    return;
  }

  mWindow.shadowStyle = mShadowStyle;
  [mWindow setEffectViewWrapperForStyle:mShadowStyle];
  [mWindow setHasShadow:aStyle != WindowShadow::None];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::SetWindowOpacity(float aOpacity) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow) {
    return;
  }

  [mWindow setAlphaValue:(CGFloat)aOpacity];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::SetColorScheme(const Maybe<ColorScheme>& aScheme) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow) {
    return;
  }
  NSAppearance* appearance =
      aScheme ? NSAppearanceForColorScheme(*aScheme) : nil;
  if (mWindow.appearance != appearance) {
    mWindow.appearance = appearance;
  }
  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

static inline CGAffineTransform GfxMatrixToCGAffineTransform(
    const gfx::Matrix& m) {
  CGAffineTransform t;
  t.a = m._11;
  t.b = m._12;
  t.c = m._21;
  t.d = m._22;
  t.tx = m._31;
  t.ty = m._32;
  return t;
}

void nsCocoaWindow::SetWindowTransform(const gfx::Matrix& aTransform) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!mWindow) {
    return;
  }

  // Calling CGSSetWindowTransform when the window is not visible results in
  // misplacing the window into doubled x,y coordinates (see bug 1448132).
  if (!mWindow.isVisible || NSIsEmptyRect(mWindow.frame)) {
    return;
  }

  if (StaticPrefs::widget_macos_window_transforms_disabled()) {
    // CGSSetWindowTransform is a private API. In case calling it causes
    // problems either now or in the future, we'll want to have an easy kill
    // switch. So we allow disabling it with a pref.
    return;
  }

  gfx::Matrix transform = aTransform;

  // aTransform is a transform that should be applied to the window relative
  // to its regular position: If aTransform._31 is 100, then we want the
  // window to be displayed 100 pixels to the right of its regular position.
  // The transform that CGSSetWindowTransform accepts has a different meaning:
  // It's used to answer the question "For the screen pixel at x,y (with the
  // origin at the top left), what pixel in the window's buffer (again with
  // origin top left) should be displayed at that position?"
  // In the example above, this means that we need to call
  // CGSSetWindowTransform with a horizontal translation of -windowPos.x - 100.
  // So we need to invert the transform and adjust it by the window's position.
  if (!transform.Invert()) {
    // Treat non-invertible transforms as the identity transform.
    transform = gfx::Matrix();
  }

  bool isIdentity = transform.IsIdentity();
  if (isIdentity && mWindowTransformIsIdentity) {
    return;
  }

  transform.PreTranslate(-mBounds.x, -mBounds.y);

  // Snap translations to device pixels, to match what we do for CSS transforms
  // and because the window server rounds down instead of to nearest.
  if (!transform.HasNonTranslation() && transform.HasNonIntegerTranslation()) {
    auto snappedTranslation = gfx::IntPoint::Round(transform.GetTranslation());
    transform =
        gfx::Matrix::Translation(snappedTranslation.x, snappedTranslation.y);
  }

  // We also need to account for the backing scale factor: aTransform is given
  // in device pixels, but CGSSetWindowTransform works with logical display
  // pixels.
  CGFloat backingScale = BackingScaleFactor();
  transform.PreScale(backingScale, backingScale);
  transform.PostScale(1 / backingScale, 1 / backingScale);

  CGSConnection cid = _CGSDefaultConnection();
  CGSSetWindowTransform(cid, [mWindow windowNumber],
                        GfxMatrixToCGAffineTransform(transform));

  mWindowTransformIsIdentity = isIdentity;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::SetInputRegion(const InputRegion& aInputRegion) {
  MOZ_ASSERT(mWindowType == WindowType::Popup,
             "This should only be called on popup windows.");
  // TODO: Somehow support aInputRegion.mMargin? Though maybe not.
  if (aInputRegion.mFullyTransparent) {
    [mWindow setIgnoresMouseEvents:YES];
  } else {
    [mWindow setIgnoresMouseEvents:NO];
  }
}

void nsCocoaWindow::SetShowsToolbarButton(bool aShow) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mWindow) [mWindow setShowsToolbarButton:aShow];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

bool nsCocoaWindow::GetSupportsNativeFullscreen() {
  return mWindow.collectionBehavior &
         NSWindowCollectionBehaviorFullScreenPrimary;
}

void nsCocoaWindow::SetSupportsNativeFullscreen(
    bool aSupportsNativeFullscreen) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mWindow) {
    // This determines whether we tell cocoa that the window supports native
    // full screen. If we do so, and another window is in native full screen,
    // this window will also appear in native full screen. We generally only
    // want to do this for primary application windows. We'll set the
    // relevant macnativefullscreen attribute on those, which will lead to us
    // being called with aSupportsNativeFullscreen set to `true` here.
    NSWindowCollectionBehavior newBehavior = [mWindow collectionBehavior];
    if (aSupportsNativeFullscreen) {
      newBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
    } else {
      newBehavior &= ~NSWindowCollectionBehaviorFullScreenPrimary;
    }
    [mWindow setCollectionBehavior:newBehavior];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::SetWindowAnimationType(
    nsIWidget::WindowAnimationType aType) {
  mAnimationType = aType;
}

void nsCocoaWindow::SetHideTitlebarSeparator(bool aHide) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (@available(macOS 11.0, *)) {
    mWindow.titlebarSeparatorStyle = aHide ? NSTitlebarSeparatorStyleNone
                                           : NSTitlebarSeparatorStyleAutomatic;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

bool nsCocoaWindow::IsMacTitlebarDirectionRTL() {
  return mWindow && mWindow.windowTitlebarLayoutDirection ==
                        NSUserInterfaceLayoutDirectionRightToLeft;
}

void nsCocoaWindow::SetCustomTitlebar(bool aState) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mWindow) {
    mWindow.drawsContentsIntoWindowFrame = aState;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::LockAspectRatio(bool aShouldLock) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (aShouldLock) {
    [mWindow setContentAspectRatio:mWindow.frame.size];
    mAspectRatioLocked = true;
  } else {
    // According to
    // https://developer.apple.com/documentation/appkit/nswindow/1419507-aspectratio,
    // aspect ratios and resize increments are mutually exclusive, and the
    // accepted way of cancelling an established aspect ratio is to set the
    // resize increments to 1.0, 1.0
    [mWindow setResizeIncrements:NSMakeSize(1.0, 1.0)];
    mAspectRatioLocked = false;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsCocoaWindow::SetPopupWindowLevel() {
  if (!mWindow) {
    return;
  }
  // Otherwise, this is a top-level or parent popup. Parent popups always
  // appear just above their parent and essentially ignore the level.
  mWindow.level = NSPopUpMenuWindowLevel;
  mWindow.hidesOnDeactivate = NO;
}

bool nsCocoaWindow::GetEditCommands(NativeKeyBindingsType aType,
                                    const WidgetKeyboardEvent& aEvent,
                                    nsTArray<CommandInt>& aCommands) {
  // Validate the arguments.
  if (NS_WARN_IF(!nsIWidget::GetEditCommands(aType, aEvent, aCommands))) {
    return false;
  }

  Maybe<WritingMode> writingMode;
  if (aEvent.NeedsToRemapNavigationKey()) {
    if (RefPtr<TextEventDispatcher> dispatcher = GetTextEventDispatcher()) {
      writingMode = dispatcher->MaybeQueryWritingModeAtSelection();
    }
  }

  NativeKeyBindings* keyBindings = NativeKeyBindings::GetInstance(aType);
  keyBindings->GetEditCommands(aEvent, writingMode, aCommands);
  return true;
}

already_AddRefed<nsIWidget> nsIWidget::CreateTopLevelWindow() {
  nsCOMPtr<nsIWidget> window = new nsCocoaWindow();
  return window.forget();
}

already_AddRefed<nsIWidget> nsIWidget::CreateChildWindow() {
  nsCOMPtr<nsIWidget> window = new nsCocoaWindow();
  return window.forget();
}

@implementation WindowDelegate

// We try to find a gecko menu bar to paint. If one does not exist, just paint
// the application menu by itself so that a window doesn't have some other
// window's menu bar.
+ (void)paintMenubarForWindow:(NSWindow*)aWindow {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!NSApp.active) {
    // Early exit if the app isn't active. This is because we can't safely
    // set the NSApp.mainMenu property in such a case. We early exit so we
    // also don't invoke any side effects.
    return;
  }

  // make sure we only act on windows that have this kind of
  // object as a delegate
  id windowDelegate = [aWindow delegate];
  if ([windowDelegate class] != [self class]) return;

  nsCocoaWindow* geckoWidget = [windowDelegate geckoWidget];
  NS_ASSERTION(geckoWidget, "Window delegate not returning a gecko widget!");

  if (nsMenuBarX* geckoMenuBar = geckoWidget->GetMenuBar()) {
    // We do an async paint in order to prevent crashes when macOS is actively
    // enumerating the menu items in `NSApp.mainMenu`.
    geckoMenuBar->PaintAsyncIfNeeded();
  } else {
    // sometimes we don't have a native application menu early in launching
    if (!sApplicationMenu) {
      return;
    }

    NSMenu* mainMenu = NSApp.mainMenu;
    NS_ASSERTION(
        mainMenu.numberOfItems > 0,
        "Main menu does not have any items, something is terribly wrong!");

    // Create a new menu bar.
    // We create a GeckoNSMenu because all menu bar NSMenu objects should use
    // that subclass for key handling reasons.
    GeckoNSMenu* newMenuBar =
        [[GeckoNSMenu alloc] initWithTitle:@"MainMenuBar"];

    // move the application menu from the existing menu bar to the new one
    NSMenuItem* firstMenuItem = [[mainMenu itemAtIndex:0] retain];
    [mainMenu removeItemAtIndex:0];
    [newMenuBar insertItem:firstMenuItem atIndex:0];
    [firstMenuItem release];

    // set our new menu bar as the main menu
    NSApp.mainMenu = newMenuBar;
    [newMenuBar release];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (id)initWithGeckoWindow:(nsCocoaWindow*)geckoWind {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  self = [super init];
  if (!self) {
    return nil;
  }

  mGeckoWindow = geckoWind;
  mToplevelActiveState = false;
  mHasEverBeenZoomed = false;

  return self;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (NSSize)windowWillResize:(NSWindow*)sender toSize:(NSSize)proposedFrameSize {
  RollUpPopups();
  return proposedFrameSize;
}

- (NSRect)windowWillUseStandardFrame:(NSWindow*)window
                        defaultFrame:(NSRect)newFrame {
  // This function needs to return a rect representing the frame a window would
  // have if it is in its "maximized" size mode. The parameter newFrame is
  // supposed to be a frame representing the maximum window size on the screen
  // where the window currently appears. However, in practice, newFrame can be a
  // much smaller size. So, we ignore newframe and instead return the frame of
  // the entire screen associated with the window. That frame is bigger than the
  // window could actually be, due to the presence of the menubar and possibly
  // the dock, but we never call this function directly, and Cocoa callers will
  // shrink it to its true maximum size.
  return window.screen.frame;
}

void nsCocoaWindow::CocoaSendToplevelActivateEvents() {
  if (mWidgetListener) {
    mWidgetListener->WindowActivated();
  }
}

void nsCocoaWindow::CocoaSendToplevelDeactivateEvents() {
  if (mWidgetListener) {
    mWidgetListener->WindowDeactivated();
  }
}

void nsCocoaWindow::CocoaWindowDidResize() {
  // It's important to update our bounds before we trigger any listeners. This
  // ensures that our bounds are correct when GetScreenBounds is called.
  UpdateBounds();

  if (mCompositorWidgetDelegate) {
    auto deviceIntRect = GetClientBounds();
    mCompositorWidgetDelegate->NotifyClientSizeChanged(deviceIntRect.Size());
  }

  if (HandleUpdateFullscreenOnResize()) {
    ReportSizeEvent();
    return;
  }

  // Resizing might have changed our zoom state.
  DispatchSizeModeEvent();
  ReportSizeEvent();
}

void nsCocoaWindow::LockNativePointer(
    NativePointerLockMode aNativePointerLockMode) {
  if (!StaticPrefs::dom_pointer_lock_native_lock_enabled()) {
    return;
  }

  if (GetNativePointerLockedMode()) {
    MOZ_ASSERT(*GetNativePointerLockedMode() == aNativePointerLockMode,
               "Should not call LockNativePointer() with a different mode "
               "whenthe pointer is already locked");
    // XXX Maybe we should avoid calling LockNativePointer() again when the
    // content changes the pointer lock element while the pointer is already
    // locked.
    return;
  }

  sNativePointerLockMode.emplace(aNativePointerLockMode);
  CGAssociateMouseAndMouseCursorPosition(false);
}

void nsCocoaWindow::UnlockNativePointer() {
  if (NS_WARN_IF(!GetNativePointerLockedMode())) {
    return;
  }

  sNativePointerLockMode.reset();
  CGAssociateMouseAndMouseCursorPosition(true);
  sNativeLockedPoint = LayoutDeviceIntPoint(0, 0);
}

void nsCocoaWindow::SetNativePointerLockMode(
    NativePointerLockMode aNativePointerLockMode) {
  if (NS_WARN_IF(!GetNativePointerLockedMode())) {
    return;
  }
  sNativePointerLockMode.ref() = aNativePointerLockMode;
}

bool nsCocoaWindow::SupportsUnadjustedMovement() {
  return StaticPrefs::dom_pointer_lock_native_lock_enabled();
}

/* static */ Maybe<nsIWidget::NativePointerLockMode>
    nsCocoaWindow::sNativePointerLockMode;
/* static */ LayoutDeviceIntPoint nsCocoaWindow::sNativeLockedPoint;

/* static */
const Maybe<nsIWidget::NativePointerLockMode>&
nsCocoaWindow::GetNativePointerLockedMode() {
  MOZ_ASSERT_IF(sNativePointerLockMode,
                StaticPrefs::dom_pointer_lock_native_lock_enabled());
  return sNativePointerLockMode;
}

/* static */
LayoutDeviceIntPoint nsCocoaWindow::GetNativeLockedPoint() {
  MOZ_ASSERT(GetNativePointerLockedMode());
  return sNativeLockedPoint;
}

- (void)windowDidResize:(NSNotification*)aNotification {
  if (!mGeckoWindow) return;

  mGeckoWindow->CocoaWindowDidResize();
}

- (void)windowDidChangeScreen:(NSNotification*)aNotification {
  if (!mGeckoWindow) return;

  // Because of Cocoa's peculiar treatment of zero-size windows (see comments
  // at GetBackingScaleFactor() above), we sometimes have a situation where
  // our concept of backing scale (based on the screen where the zero-sized
  // window is positioned) differs from Cocoa's idea (always based on the
  // Retina screen, AFAICT, even when an external non-Retina screen is the
  // primary display).
  //
  // As a result, if the window was created with zero size on an external
  // display, but then made visible on the (secondary) Retina screen, we
  // will *not* get a windowDidChangeBackingProperties notification for it.
  // This leads to an incorrect GetDefaultScale(), and widget coordinate
  // confusion, as per bug 853252.
  //
  // To work around this, we check for a backing scale mismatch when we
  // receive a windowDidChangeScreen notification, as we will receive this
  // even if Cocoa was already treating the zero-size window as having
  // Retina backing scale. Note that BackingScaleFactorChanged() bails early
  // if the scale factor did in fact not change.
  mGeckoWindow->BackingScaleFactorChanged();
  mGeckoWindow->ReportMoveEvent();
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification {
  if (!mGeckoWindow) {
    return;
  }
  mGeckoWindow->CocoaWindowWillEnterFullscreen(true);
}

// Lion's full screen mode will bypass our internal fullscreen tracking, so
// we need to catch it when we transition and call our own methods, which in
// turn will fire "fullscreen" events.
- (void)windowDidEnterFullScreen:(NSNotification*)notification {
  // On Yosemite, the NSThemeFrame class has two new properties --
  // titlebarView (an NSTitlebarView object) and titlebarContainerView (an
  // NSTitlebarContainerView object).  These are used to display the titlebar
  // in fullscreen mode.  In Safari they're not transparent.  But in Firefox
  // for some reason they are, which causes bug 1069658.  The following code
  // works around this Apple bug or design flaw.
  NSWindow* window = notification.object;
  NSView* frameView = window.contentView.superview;
  NSView* titlebarView = nil;
  NSView* titlebarContainerView = nil;
  if ([frameView respondsToSelector:@selector(titlebarView)]) {
    titlebarView = [frameView titlebarView];
  }
  if ([frameView respondsToSelector:@selector(titlebarContainerView)]) {
    titlebarContainerView = [frameView titlebarContainerView];
  }
  if ([titlebarView respondsToSelector:@selector(setTransparent:)]) {
    [titlebarView setTransparent:NO];
  }
  if ([titlebarContainerView respondsToSelector:@selector(setTransparent:)]) {
    [titlebarContainerView setTransparent:NO];
  }

  if (@available(macOS 11.0, *)) {
    if ([window isKindOfClass:[ToolbarWindow class]]) {
      // In order to work around a drawing bug with windows in full screen
      // mode, disable titlebar separators for full screen windows of the
      // ToolbarWindow class. The drawing bug was filed as FB9056136. See bug
      // 1700211 and bug 1912338 for more details.
      window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    }
  }

  if (!mGeckoWindow) {
    return;
  }
  mGeckoWindow->CocoaWindowDidEnterFullscreen(true);
}

- (void)windowWillExitFullScreen:(NSNotification*)notification {
  if (!mGeckoWindow) {
    return;
  }
  mGeckoWindow->CocoaWindowWillEnterFullscreen(false);
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
  if (!mGeckoWindow) {
    return;
  }
  mGeckoWindow->CocoaWindowDidEnterFullscreen(false);
}

- (void)windowDidFailToEnterFullScreen:(NSNotification*)notification {
  if (!mGeckoWindow) {
    return;
  }

  MOZ_ASSERT((mGeckoWindow->GetCocoaWindow().styleMask &
              NSWindowStyleMaskFullScreen) == 0);
  MOZ_ASSERT(mGeckoWindow->SizeMode() == nsSizeMode_Fullscreen);

  // We're in a strange situation. We've told DOM that we are going to
  // fullscreen by changing our size mode, and therefore the window
  // content is what we would show if we were properly in fullscreen.
  // But the window is actually in a windowed style. We have to do
  // several things:
  // 1) Clear sWindowInNativeTransition and mTransitionCurrent, both set
  //    when we started the fullscreen transition.
  // 2) Change our size mode to windowed.
  // Conveniently, we can do these things by pretending we just arrived
  // at windowed mode, and all will be sorted out.
  mGeckoWindow->CocoaWindowDidEnterFullscreen(false);
}

- (void)windowDidFailToExitFullScreen:(NSNotification*)notification {
  if (!mGeckoWindow) {
    return;
  }
  // Similarly to windowDidFailToEnterFullScreen, we can get the right
  // result by pretending we just entered fullscreen.
  mGeckoWindow->CocoaWindowDidEnterFullscreen(true);
}

- (void)windowDidBecomeMain:(NSNotification*)aNotification {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  RollUpPopups();
  ChildViewMouseTracker::ReEvaluateMouseEnterState();

  // [NSApp _isRunningAppModal] will return true if we're running an OS dialog
  // app modally. If one of those is up then we want it to retain its menu bar.
  if (NSApp._isRunningAppModal) {
    return;
  }
  NSWindow* window = aNotification.object;
  if (window) {
    [WindowDelegate paintMenubarForWindow:window];
  }

  if ([window isKindOfClass:[ToolbarWindow class]]) {
    [(ToolbarWindow*)window windowMainStateChanged];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)windowDidResignMain:(NSNotification*)aNotification {
  RollUpPopups();
  ChildViewMouseTracker::ReEvaluateMouseEnterState();

  // [NSApp _isRunningAppModal] will return true if we're running an OS dialog
  // app modally. If one of those is up then we want it to retain its menu bar.
  if ([NSApp _isRunningAppModal]) return;
  RefPtr<nsMenuBarX> hiddenWindowMenuBar =
      nsMenuUtilsX::GetHiddenWindowMenuBar();
  if (hiddenWindowMenuBar) {
    bool isTerminating = false;
    nsCOMPtr<nsIAppStartup> appStartup(components::AppStartup::Service());
    if (appStartup) {
      appStartup->GetAttemptingQuit(&isTerminating);
    }

    if (!isTerminating) {
      // We do an async paint in order to prevent crashes when macOS is actively
      // enumerating the menu items in `NSApp.mainMenu`.
      hiddenWindowMenuBar->PaintAsyncIfNeeded();
    }
  }

  NSWindow* window = [aNotification object];
  if ([window isKindOfClass:[ToolbarWindow class]]) {
    [(ToolbarWindow*)window windowMainStateChanged];
  }
}

- (void)windowDidBecomeKey:(NSNotification*)aNotification {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  RollUpPopups();
  ChildViewMouseTracker::ReEvaluateMouseEnterState();

  if (!mGeckoWindow) {
    return;
  }
  if (mGeckoWindow->GetInputContext().IsPasswordEditor()) {
    TextInputHandler::EnableSecureEventInput();
  } else {
    TextInputHandler::EnsureSecureEventInputDisabled();
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)windowDidResignKey:(NSNotification*)aNotification {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  RollUpPopups(nsIRollupListener::AllowAnimations::No);

  ChildViewMouseTracker::ReEvaluateMouseEnterState();
  TextInputHandler::EnsureSecureEventInputDisabled();

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)windowWillMove:(NSNotification*)aNotification {
  RollUpPopups();
}

- (void)windowDidMove:(NSNotification*)aNotification {
  if (mGeckoWindow) mGeckoWindow->ReportMoveEvent();
}

- (BOOL)windowShouldClose:(id)sender {
  nsIWidgetListener* listener =
      mGeckoWindow ? mGeckoWindow->GetWidgetListener() : nullptr;
  if (listener) listener->RequestWindowClose(mGeckoWindow);
  return NO;  // gecko will do it
}

- (void)windowWillClose:(NSNotification*)aNotification {
  RollUpPopups();
}

- (void)windowWillMiniaturize:(NSNotification*)aNotification {
  RollUpPopups();
}

- (void)windowDidMiniaturize:(NSNotification*)aNotification {
  if (!mGeckoWindow) {
    return;
  }
  mGeckoWindow->FinishCurrentTransitionIfMatching(
      nsCocoaWindow::TransitionType::Miniaturize);
}

- (void)windowDidDeminiaturize:(NSNotification*)aNotification {
  if (!mGeckoWindow) {
    return;
  }
  mGeckoWindow->FinishCurrentTransitionIfMatching(
      nsCocoaWindow::TransitionType::Deminiaturize);
}

- (BOOL)windowShouldZoom:(NSWindow*)window toFrame:(NSRect)proposedFrame {
  if (!mHasEverBeenZoomed && window.isZoomed) {
    return NO;  // See bug 429954.
  }
  mHasEverBeenZoomed = YES;
  return YES;
}

- (void)windowDidChangeBackingProperties:(NSNotification*)aNotification {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  mGeckoWindow->BackingScaleFactorChanged();

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// This method is on NSWindowDelegate starting with 10.9
- (void)windowDidChangeOcclusionState:(NSNotification*)aNotification {
  if (mGeckoWindow) {
    mGeckoWindow->DispatchOcclusionEvent();
  }
}

- (nsCocoaWindow*)geckoWidget {
  return mGeckoWindow;
}

- (bool)toplevelActiveState {
  return mToplevelActiveState;
}

- (void)sendToplevelActivateEvents {
  if (!mToplevelActiveState && mGeckoWindow) {
    mGeckoWindow->CocoaSendToplevelActivateEvents();

    mToplevelActiveState = true;
  }
}

- (void)sendToplevelDeactivateEvents {
  if (mToplevelActiveState && mGeckoWindow) {
    mGeckoWindow->CocoaSendToplevelDeactivateEvents();
    mToplevelActiveState = false;
  }
}

@end

@interface NSView (FrameViewMethodSwizzling)
- (NSPoint)FrameView__closeButtonOrigin;
- (CGFloat)FrameView__titlebarHeight;
@end

@implementation NSView (FrameViewMethodSwizzling)

- (NSPoint)FrameView__closeButtonOrigin {
  if (![self.window isKindOfClass:[ToolbarWindow class]]) {
    return self.FrameView__closeButtonOrigin;
  }
  auto* win = static_cast<ToolbarWindow*>(self.window);
  if (win.drawsContentsIntoWindowFrame &&
      !(win.styleMask & NSWindowStyleMaskFullScreen) &&
      (win.styleMask & NSWindowStyleMaskTitled)) {
    const NSRect buttonsRect = win.windowButtonsRect;
    if (NSIsEmptyRect(buttonsRect)) {
      // Empty rect. Let's hide the buttons.
      // Position is in non-flipped window coordinates. Using frame's height
      // for the vertical coordinate will move the buttons above the window,
      // making them invisible.
      return NSMakePoint(buttonsRect.origin.x, win.frame.size.height);
    }
    if (win.windowTitlebarLayoutDirection ==
        NSUserInterfaceLayoutDirectionRightToLeft) {
      // We're in RTL mode, which means that the close button is the rightmost
      // button of the three window buttons. and buttonsRect.origin is the
      // bottom left corner of the green (zoom) button. The close button is 40px
      // to the right of the zoom button. This is confirmed to be the same on
      // all macOS versions between 10.12 - 12.0.
      return NSMakePoint(buttonsRect.origin.x + 40.0f, buttonsRect.origin.y);
    }
    return buttonsRect.origin;
  }
  return self.FrameView__closeButtonOrigin;
}

- (CGFloat)FrameView__titlebarHeight {
  // XXX: Shouldn't this be [super FrameView__titlebarHeight]?
  CGFloat height = [self FrameView__titlebarHeight];
  if ([self.window isKindOfClass:[ToolbarWindow class]]) {
    // Make sure that the titlebar height includes our shifted buttons.
    // The following coordinates are in window space, with the origin being at
    // the bottom left corner of the window.
    auto* win = static_cast<ToolbarWindow*>(self.window);
    CGFloat frameHeight = self.frame.size.height;
    CGFloat windowButtonY = frameHeight;
    if (!NSIsEmptyRect(win.windowButtonsRect) &&
        win.drawsContentsIntoWindowFrame &&
        !(win.styleMask & NSWindowStyleMaskFullScreen) &&
        (win.styleMask & NSWindowStyleMaskTitled)) {
      windowButtonY = win.windowButtonsRect.origin.y;
    }
    height = std::max(height, frameHeight - windowButtonY);
  }
  return height;
}

@end

static NSMutableSet* gSwizzledFrameViewClasses = nil;

@interface NSWindow (PrivateSetNeedsDisplayInRectMethod)
- (void)_setNeedsDisplayInRect:(NSRect)aRect;
@end

@interface BaseWindow (Private)
- (void)cursorUpdated:(NSEvent*)aEvent;
- (void)reflowTitlebarElements;
@end

@implementation BaseWindow

// The frame of a window is implemented using undocumented NSView subclasses.
// We offset the window buttons by overriding the method _closeButtonOrigin on
// these frame view classes. The class which is
// used for a window is determined in the window's frameViewClassForStyleMask:
// method, so this is where we make sure that we have swizzled the method on
// all encountered classes.
+ (Class)frameViewClassForStyleMask:(NSUInteger)styleMask {
  Class frameViewClass = [super frameViewClassForStyleMask:styleMask];

  if (!gSwizzledFrameViewClasses) {
    gSwizzledFrameViewClasses = [[NSMutableSet setWithCapacity:3] retain];
    if (!gSwizzledFrameViewClasses) {
      return frameViewClass;
    }
  }

  MOZ_RUNINIT static IMP our_closeButtonOrigin = class_getMethodImplementation(
      [NSView class], @selector(FrameView__closeButtonOrigin));
  MOZ_RUNINIT static IMP our_titlebarHeight = class_getMethodImplementation(
      [NSView class], @selector(FrameView__titlebarHeight));

  if (![gSwizzledFrameViewClasses containsObject:frameViewClass]) {
    // Either of these methods might be implemented in both a subclass of
    // NSFrameView and one of its own subclasses.  Which means that if we
    // aren't careful we might end up swizzling the same method twice.
    // Since method swizzling involves swapping pointers, this would break
    // things.
    IMP _closeButtonOrigin = class_getMethodImplementation(
        frameViewClass, @selector(_closeButtonOrigin));
    if (_closeButtonOrigin && _closeButtonOrigin != our_closeButtonOrigin) {
      nsToolkit::SwizzleMethods(frameViewClass, @selector(_closeButtonOrigin),
                                @selector(FrameView__closeButtonOrigin));
    }

    // Override _titlebarHeight so that the floating titlebar doesn't clip the
    // bottom of the window buttons which we move down with our override of
    // _closeButtonOrigin.
    IMP _titlebarHeight = class_getMethodImplementation(
        frameViewClass, @selector(_titlebarHeight));
    if (_titlebarHeight && _titlebarHeight != our_titlebarHeight) {
      nsToolkit::SwizzleMethods(frameViewClass, @selector(_titlebarHeight),
                                @selector(FrameView__titlebarHeight));
    }

    [gSwizzledFrameViewClasses addObject:frameViewClass];
  }

  return frameViewClass;
}

- (id)initWithContentRect:(NSRect)aContentRect
                styleMask:(NSUInteger)aStyle
                  backing:(NSBackingStoreType)aBufferingType
                    defer:(BOOL)aFlag {
  self = [super initWithContentRect:aContentRect
                          styleMask:aStyle
                            backing:aBufferingType
                              defer:aFlag];
  if (!self) {
    return nil;
  }

  mDrawsIntoWindowFrame = NO;
  mState = nil;
  mDisabledNeedsDisplay = NO;
  mTrackingArea = nil;
  mViewWithTrackingArea = nil;
  mDirtyRect = NSZeroRect;
  mBeingShown = NO;
  mTouchBar = nil;
  mIsAnimationSuppressed = NO;

  return self;
}

static CGFloat GetMenuCornerRadius() {
  return nsCocoaFeatures::OnTahoeOrLater() ? 12.0f : 6.0f;
}

// Returns an autoreleased NSImage.
static NSImage* GetMenuMaskImage() {
  const CGFloat radius = GetMenuCornerRadius();
  const NSSize maskSize = {radius * 3.0f, radius * 3.0f};
  NSImage* maskImage = [NSImage imageWithSize:maskSize
                                      flipped:FALSE
                               drawingHandler:^BOOL(NSRect dstRect) {
                                 NSBezierPath* path = [NSBezierPath
                                     bezierPathWithRoundedRect:dstRect
                                                       xRadius:radius
                                                       yRadius:radius];
                                 [NSColor.blackColor set];
                                 [path fill];
                                 return YES;
                               }];
  maskImage.capInsets = NSEdgeInsetsMake(radius, radius, radius, radius);
  return maskImage;
}

// Add an effect view wrapper if needed so that the OS draws the appropriate
// vibrancy effect and window border.
- (void)setEffectViewWrapperForStyle:(WindowShadow)aStyle {
  NSView* wrapper = [&]() -> NSView* {
    if (@available(macOS 26.0, *)) {
      if (aStyle == WindowShadow::Menu) {
        // Menus on macOS 26 use glass instead of vibrancy.
        auto* effectView =
            [[NSGlassEffectView alloc] initWithFrame:self.contentView.frame];
        effectView.cornerRadius = GetMenuCornerRadius();
        return effectView;
      }
    }
    if (aStyle == WindowShadow::Menu || aStyle == WindowShadow::Tooltip) {
      const bool isMenu = aStyle == WindowShadow::Menu;
      auto* effectView =
          [[NSVisualEffectView alloc] initWithFrame:self.contentView.frame];
      effectView.material =
          isMenu ? NSVisualEffectMaterialMenu : NSVisualEffectMaterialToolTip;
      // Tooltip and menu windows are never "key", so we need to tell the
      // vibrancy effect to look active regardless of window state.
      effectView.state = NSVisualEffectStateActive;
      effectView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
      if (isMenu) {
        // Turn on rounded corner masking.
        effectView.maskImage = GetMenuMaskImage();
      }
      return effectView;
    }
    return [[NSView alloc] initWithFrame:self.contentView.frame];
  }();

  wrapper.wantsLayer = YES;
  // Swap out our content view by the new view. Setting .contentView releases
  // the old view.
  NSView* childView = [self.mainChildView retain];
  [childView removeFromSuperview];
  [wrapper addSubview:childView];
  [childView release];
  super.contentView = wrapper;
  [wrapper release];
}

- (NSTouchBar*)makeTouchBar {
  [mTouchBar release];
  mTouchBar = [[nsTouchBar alloc] init];
  if (mTouchBar) {
    sTouchBarIsInitialized = YES;
  }
  return mTouchBar;
}

- (void)setIsBeingShown:(BOOL)aValue {
  mBeingShown = aValue;
}

- (BOOL)isBeingShown {
  return mBeingShown;
}

- (BOOL)isVisibleOrBeingShown {
  return [super isVisible] || mBeingShown;
}

- (void)setIsAnimationSuppressed:(BOOL)aValue {
  mIsAnimationSuppressed = aValue;
}

- (BOOL)isAnimationSuppressed {
  return mIsAnimationSuppressed;
}

- (void)disableSetNeedsDisplay {
  mDisabledNeedsDisplay = YES;
}

- (void)enableSetNeedsDisplay {
  mDisabledNeedsDisplay = NO;
}

- (void)dealloc {
  [mTouchBar release];
  ChildViewMouseTracker::OnDestroyWindow(self);
  [super dealloc];
}

static const NSString* kStateTitleKey = @"title";
static const NSString* kStateDrawsContentsIntoWindowFrameKey =
    @"drawsContentsIntoWindowFrame";
static const NSString* kStateShowsToolbarButton = @"showsToolbarButton";
static const NSString* kStateCollectionBehavior = @"collectionBehavior";

- (void)importState:(NSDictionary*)aState {
  if (NSString* title = [aState objectForKey:kStateTitleKey]) {
    [self setTitle:title];
  }
  [self setDrawsContentsIntoWindowFrame:
            [[aState objectForKey:kStateDrawsContentsIntoWindowFrameKey]
                boolValue]];
  [self setShowsToolbarButton:[[aState objectForKey:kStateShowsToolbarButton]
                                  boolValue]];
  [self setCollectionBehavior:[[aState objectForKey:kStateCollectionBehavior]
                                  unsignedIntValue]];
}

- (NSMutableDictionary*)exportState {
  NSMutableDictionary* state = [NSMutableDictionary dictionaryWithCapacity:10];
  if (NSString* title = self.title) {
    [state setObject:title forKey:kStateTitleKey];
  }
  [state setObject:[NSNumber numberWithBool:self.drawsContentsIntoWindowFrame]
            forKey:kStateDrawsContentsIntoWindowFrameKey];
  [state setObject:[NSNumber numberWithBool:self.showsToolbarButton]
            forKey:kStateShowsToolbarButton];
  [state setObject:[NSNumber numberWithUnsignedInt:self.collectionBehavior]
            forKey:kStateCollectionBehavior];
  return state;
}

- (void)setDrawsContentsIntoWindowFrame:(BOOL)aState {
  bool changed = aState != mDrawsIntoWindowFrame;
  mDrawsIntoWindowFrame = aState;
  if (changed) {
    [self reflowTitlebarElements];
    [self updateTitlebarTransparency];
  }
}

- (BOOL)drawsContentsIntoWindowFrame {
  return mDrawsIntoWindowFrame;
}

- (NSRect)childViewRectForFrameRect:(NSRect)aFrameRect {
  if (mDrawsIntoWindowFrame) {
    return aFrameRect;
  }
  NSUInteger styleMask = [self styleMask];
  styleMask &= ~NSWindowStyleMaskFullSizeContentView;
  return [NSWindow contentRectForFrameRect:aFrameRect styleMask:styleMask];
}

// relative to the window frame rect, with the origin in the bottom left.
- (NSRect)childViewFrameRectForCurrentBounds {
  auto frame = self.frame;
  NSRect r = [self childViewRectForFrameRect:frame];
  r.origin.x -= frame.origin.x;
  r.origin.y -= frame.origin.y;
  return r;
}

- (void)updateChildViewFrameRect {
  self.mainChildView.frame = self.childViewFrameRectForCurrentBounds;
}

- (NSRect)frameRectForChildViewRect:(NSRect)aChildViewRect {
  if (mDrawsIntoWindowFrame) {
    return aChildViewRect;
  }
  NSUInteger styleMask = [self styleMask];
  styleMask &= ~NSWindowStyleMaskFullSizeContentView;
  return [NSWindow frameRectForContentRect:aChildViewRect styleMask:styleMask];
}

- (NSTimeInterval)animationResizeTime:(NSRect)newFrame {
  if (mIsAnimationSuppressed) {
    // Should not animate the initial session-restore size change
    return 0.0;
  }

  return [super animationResizeTime:newFrame];
}

- (NSView*)trackingAreaView {
  NSView* contentView = self.contentView;
  return contentView.superview ? contentView.superview : contentView;
}

- (NSArray<NSView*>*)contentViewContents {
  return [[self.contentView.subviews copy] autorelease];
}

- (ChildView*)mainChildView {
  NSView* contentView = self.contentView;
  NSView* lastView = contentView.subviews.lastObject;
  if ([lastView isKindOfClass:[ChildView class]]) {
    return (ChildView*)lastView;
  }
  return nil;
}

- (void)removeTrackingArea {
  [mViewWithTrackingArea removeTrackingArea:mTrackingArea];

  [mTrackingArea release];
  mTrackingArea = nil;

  [mViewWithTrackingArea release];
  mViewWithTrackingArea = nil;
}

- (void)createTrackingArea {
  mViewWithTrackingArea = [self.trackingAreaView retain];
  const NSTrackingAreaOptions options =
      NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
      NSTrackingActiveAlways | NSTrackingInVisibleRect;
  mTrackingArea =
      [[NSTrackingArea alloc] initWithRect:[mViewWithTrackingArea bounds]
                                   options:options
                                     owner:self
                                  userInfo:nil];
  [mViewWithTrackingArea addTrackingArea:mTrackingArea];
}

- (void)mouseEntered:(NSEvent*)aEvent {
  ChildViewMouseTracker::MouseEnteredWindow(aEvent);
}

- (void)mouseExited:(NSEvent*)aEvent {
  ChildViewMouseTracker::MouseExitedWindow(aEvent);
}

- (void)mouseMoved:(NSEvent*)aEvent {
  ChildViewMouseTracker::MouseMoved(aEvent);
}

- (void)cursorUpdated:(NSEvent*)aEvent {
  // Nothing to do here, but NSTrackingArea wants us to implement this method.
}

- (void)_setNeedsDisplayInRect:(NSRect)aRect {
  // Prevent unnecessary invalidations due to moving NSViews (e.g. for plugins)
  if (!mDisabledNeedsDisplay) {
    // This method is only called by Cocoa, so when we're here, we know that
    // it's available and don't need to check whether our superclass responds
    // to the selector.
    [super _setNeedsDisplayInRect:aRect];
    mDirtyRect = NSUnionRect(mDirtyRect, aRect);
  }
}

- (NSRect)getAndResetNativeDirtyRect {
  NSRect dirtyRect = mDirtyRect;
  mDirtyRect = NSZeroRect;
  return dirtyRect;
}

// Possibly move the titlebar buttons.
- (void)reflowTitlebarElements {
  NSView* frameView = self.contentView.superview;
  if ([frameView respondsToSelector:@selector(_tileTitlebarAndRedisplay:)]) {
    [frameView _tileTitlebarAndRedisplay:NO];
  }
}

- (void)updateTitlebarTransparency {
  if (self.drawsContentsIntoWindowFrame) {
    // Hide the titlebar if we are drawing into it
    self.titlebarAppearsTransparent = true;
    return;
  }

  if (@available(macOS 26.0, *)) {
    // On macOS 26, the titlebar must be transparent to hide the separator.
    // This does not affect the titlebar background on macOS 26, as it
    // already matches the window background even when not transparent.
    if (self.titlebarSeparatorStyle == NSTitlebarSeparatorStyleNone) {
      self.titlebarAppearsTransparent = true;
      return;
    }
  }

  // Show the titlebar otherwise.
  self.titlebarAppearsTransparent = false;
}

- (void)setTitlebarSeparatorStyle:(NSTitlebarSeparatorStyle)aStyle {
  [super setTitlebarSeparatorStyle:aStyle];
  [self updateTitlebarTransparency];
}

- (BOOL)respondsToSelector:(SEL)aSelector {
  // Claim the window doesn't respond to this so that the system
  // doesn't steal keyboard equivalents for it. Bug 613710.
  if (aSelector == @selector(cancelOperation:)) {
    return NO;
  }

  return [super respondsToSelector:aSelector];
}

- (void)doCommandBySelector:(SEL)aSelector {
  // We override this so that it won't beep if it can't act.
  // We want to control the beeping for missing or disabled
  // commands ourselves.
  [self tryToPerform:aSelector with:nil];
}

- (id)accessibilityAttributeValue:(NSString*)attribute {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if ([self isKindOfClass:[PopupWindow class]]) {
    if ([attribute isEqualToString:NSAccessibilityRoleAttribute]) {
      // If this is a popup window give it a proper role so VoiceOver
      // picks it up correctly.
      return NSAccessibilityPopoverRole;
    }
  }

  id retval = [super accessibilityAttributeValue:attribute];

  // The following works around a problem with Text-to-Speech on macOS 10.7.
  // See bug 674612 for more info.
  //
  // When accessibility is off, AXUIElementCopyAttributeValue(), when called
  // on an AXApplication object to get its AXFocusedUIElement attribute,
  // always returns an AXWindow object (the actual browser window -- never a
  // mozAccessible object).  This also happens with accessibility turned on,
  // if no other object in the browser window has yet been focused.  But if
  // the browser window has a title bar (as it currently always does), the
  // AXWindow object will always have four "accessible" children, one of which
  // is an AXStaticText object (the title bar's "title"; the other three are
  // the close, minimize and zoom buttons).  This means that (for complicated
  // reasons, for which see bug 674612) Text-to-Speech on macOS 10.7 will often
  // "speak" the window title, no matter what text is selected, or even if no
  // text at all is selected.  (This always happens when accessibility is off.
  // It doesn't happen in Firefox releases because Apple has (on macOS 10.7)
  // special-cased the handling of apps whose CFBundleIdentifier is
  // org.mozilla.firefox.)
  //
  // We work around this problem by only returning AXChildren that are
  // mozAccessible object or are one of the titlebar's buttons (which
  // instantiate subclasses of NSButtonCell).
  if ([retval isKindOfClass:[NSArray class]] &&
      [attribute isEqualToString:@"AXChildren"]) {
    NSMutableArray* holder = [NSMutableArray arrayWithCapacity:10];
    [holder addObjectsFromArray:(NSArray*)retval];
    NSUInteger count = [holder count];
    for (NSInteger i = count - 1; i >= 0; --i) {
      id item = [holder objectAtIndex:i];
      // Remove anything from holder that isn't one of the titlebar's buttons
      // (which instantiate subclasses of NSButtonCell) or a mozAccessible
      // object (or one of its subclasses).
      if (![item isKindOfClass:[NSButtonCell class]] &&
          ![item respondsToSelector:@selector(hasRepresentedView)]) {
        [holder removeObjectAtIndex:i];
      }
    }
    retval = [NSArray arrayWithArray:holder];
  }

  return retval;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (BOOL)isAccessibilityElement {
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return [super isAccessibilityElement];

  // If the main child view does not have a gecko accessible associated with it,
  // this window should not be present in the accessible tree.
  return [self.mainChildView hasMozAccessible];
}

- (void)releaseJSObjects {
  [mTouchBar releaseJSObjects];
}

@end

@interface MOZTitlebarAccessoryView : NSView
@end

@implementation MOZTitlebarAccessoryView : NSView
- (void)viewWillMoveToWindow:(NSWindow*)aWindow {
  if (aWindow) {
    // When entering full screen mode, titlebar accessory views are inserted
    // into a floating NSWindow which houses the window titlebar and toolbars.
    // In order to work around a drawing bug with windows in full screen mode,
    // disable titlebar separators for all NSWindows that this view is used in
    // that are not of the ToolbarWindow class, such as the floating full
    // screen toolbar window. The drawing bug was filed as FB9056136. See bug
    // 1700211 and bug 1912338 for more details.
    if (@available(macOS 11.0, *)) {
      aWindow.titlebarSeparatorStyle =
          [aWindow isKindOfClass:[ToolbarWindow class]]
              ? NSTitlebarSeparatorStyleAutomatic
              : NSTitlebarSeparatorStyleNone;
    }
  }
}
@end

@implementation FullscreenTitlebarTracker
- (FullscreenTitlebarTracker*)init {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.hidden = YES;

  return self;
}
- (void)loadView {
  self.view =
      [[[MOZTitlebarAccessoryView alloc] initWithFrame:NSZeroRect] autorelease];
}
@end

// Drop all mouse events if a modal window has appeared above us.
// This helps make us behave as if the OS were running a "real" modal event
// loop.
static bool MaybeDropEventForModalWindow(NSEvent* aEvent, id aDelegate) {
  if (!sModalWindowCount) {
    return false;
  }

  NSEventType type = [aEvent type];
  switch (type) {
    case NSEventTypeScrollWheel:
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp:
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
      break;
    default:
      return false;
  }

  if (aDelegate && [aDelegate isKindOfClass:[WindowDelegate class]]) {
    if (nsCocoaWindow* widget = [(WindowDelegate*)aDelegate geckoWidget]) {
      if (!widget->IsModal() || widget->HasModalDescendants()) {
        return true;
      }
    }
  }
  return false;
}

@implementation ToolbarWindow

- (id)initWithContentRect:(NSRect)aChildViewRect
                styleMask:(NSUInteger)aStyle
                  backing:(NSBackingStoreType)aBufferingType
                    defer:(BOOL)aFlag {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // We treat aChildViewRect as the rectangle that the window's main ChildView
  // should be sized to. Get the right frameRect for the requested child view
  // rect.
  NSRect frameRect = [NSWindow frameRectForContentRect:aChildViewRect
                                             styleMask:aStyle];

  // Always size the content view to the full frame size of the window.
  // We do this even if we want this window to have a titlebar; in that case,
  // the window's content view covers the entire window but the ChildView inside
  // it will only cover the content area. We do this so that we can render the
  // titlebar gradient manually, with a subview of our content view that's
  // positioned in the titlebar area. This lets us have a smooth connection
  // between titlebar and toolbar gradient in case the window has a "unified
  // toolbar + titlebar" look. Moreover, always using a full size content view
  // lets us toggle the titlebar on and off without changing the window's style
  // mask (which would have other subtle effects, for example on keyboard
  // focus).
  aStyle |= NSWindowStyleMaskFullSizeContentView;

  // -[NSWindow initWithContentRect:styleMask:backing:defer:] calls
  // [self frameRectForContentRect:styleMask:] to convert the supplied content
  // rect to the window's frame rect. We've overridden that method to be a
  // pass-through function. So, in order to get the intended frameRect, we need
  // to supply frameRect itself as the "content rect".
  NSRect contentRect = frameRect;

  if ((self = [super initWithContentRect:contentRect
                               styleMask:aStyle
                                 backing:aBufferingType
                                   defer:aFlag])) {
    mWindowButtonsRect = NSZeroRect;

    mFullscreenTitlebarTracker = [[FullscreenTitlebarTracker alloc] init];
    // revealAmount is an undocumented property of
    // NSTitlebarAccessoryViewController that updates whenever the menubar
    // slides down in fullscreen mode.
    [mFullscreenTitlebarTracker addObserver:self
                                 forKeyPath:@"revealAmount"
                                    options:NSKeyValueObservingOptionNew
                                    context:nil];
    // Adding this accessory view controller allows us to shift the toolbar down
    // when the user mouses to the top of the screen in fullscreen.
    [(NSWindow*)self
        addTitlebarAccessoryViewController:mFullscreenTitlebarTracker];
  }
  return self;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change
                       context:(void*)context {
  if ([keyPath isEqualToString:@"revealAmount"]) {
    [[self mainChildView] ensureNextCompositeIsAtomicWithMainThreadPaint];
    NSNumber* revealAmount = (change[NSKeyValueChangeNewKey]);
    [self updateTitlebarShownAmount:[revealAmount doubleValue]];
  } else {
    [super observeValueForKeyPath:keyPath
                         ofObject:object
                           change:change
                          context:context];
  }
}

static bool ScreenHasNotch(nsCocoaWindow* aGeckoWindow) {
  if (@available(macOS 12.0, *)) {
    nsCOMPtr<nsIScreen> widgetScreen = aGeckoWindow->GetWidgetScreen();
    NSScreen* cocoaScreen =
        ScreenHelperCocoa::CocoaScreenForScreen(widgetScreen);
    return cocoaScreen.safeAreaInsets.top != 0.0f;
  }
  return false;
}

static bool ShouldShiftByMenubarHeightInFullscreen(nsCocoaWindow* aWindow) {
  switch (StaticPrefs::widget_macos_shift_by_menubar_on_fullscreen()) {
    case 0:
      return false;
    case 1:
      return true;
    default:
      break;
  }
  return !ScreenHasNotch(aWindow) &&
         ![NSUserDefaults.standardUserDefaults
             integerForKey:@"AppleMenuBarVisibleInFullscreen"];
}

static CGFloat DefaultTitlebarHeight() {
  static CGFloat sDefaultHeight = [] {
    NSWindow* window =
        [[NSWindow alloc] initWithContentRect:NSZeroRect
                                    styleMask:NSWindowStyleMaskTitled
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    CGFloat height = window.frame.size.height;
    [window release];
    return height;
  }();
  return sDefaultHeight;
}

- (void)updateTitlebarShownAmount:(CGFloat)aShownAmount {
  if (!(self.styleMask & NSWindowStyleMaskFullScreen)) {
    // We are not interested in the size of the titlebar unless we are in
    // fullscreen.
    return;
  }

  // [NSApp mainMenu] menuBarHeight] returns one of two values: the full height
  // if the menubar is shown or is in the process of being shown, and 0
  // otherwise. Since we are multiplying the menubar height by aShownAmount, we
  // always want the full height.
  CGFloat menuBarHeight = NSApp.mainMenu.menuBarHeight;
  if (menuBarHeight > 0.0f) {
    mMenuBarHeight = menuBarHeight;
  }
  if ([[self delegate] isKindOfClass:[WindowDelegate class]]) {
    WindowDelegate* windowDelegate = (WindowDelegate*)[self delegate];
    nsCocoaWindow* geckoWindow = [windowDelegate geckoWidget];
    if (!geckoWindow) {
      return;
    }

    if (nsIWidgetListener* listener = geckoWindow->GetWidgetListener()) {
      // titlebarHeight returns 0 when we're in fullscreen, return the default
      // titlebar height.
      CGFloat shiftByPixels = DefaultTitlebarHeight() * aShownAmount;
      if (ShouldShiftByMenubarHeightInFullscreen(geckoWindow)) {
        shiftByPixels += mMenuBarHeight * aShownAmount;
      }
      // Use desktop pixels rather than the DesktopToLayoutDeviceScale in
      // nsCocoaWindow. The latter accounts for screen DPI. We don't want that
      // because the revealAmount property already accounts for it, so we'd be
      // compounding DPI scales > 1.
      listener->MacFullscreenMenubarOverlapChanged(DesktopCoord(shiftByPixels));
    }
  }
}

- (void)dealloc {
  [mFullscreenTitlebarTracker removeObserver:self forKeyPath:@"revealAmount"];
  [mFullscreenTitlebarTracker removeFromParentViewController];
  [mFullscreenTitlebarTracker release];

  [super dealloc];
}

- (NSArray<NSView*>*)contentViewContents {
  return [[self.contentView.subviews copy] autorelease];
}

- (void)windowMainStateChanged {
  [[self mainChildView] ensureNextCompositeIsAtomicWithMainThreadPaint];
}

// Extending the content area into the title bar works by resizing the
// mainChildView so that it covers the titlebar.
- (void)setDrawsContentsIntoWindowFrame:(BOOL)aState {
  BOOL stateChanged = self.drawsContentsIntoWindowFrame != aState;
  [super setDrawsContentsIntoWindowFrame:aState];
  if (stateChanged && [self.delegate isKindOfClass:[WindowDelegate class]]) {
    self.titleVisibility = aState ? NSWindowTitleHidden : NSWindowTitleVisible;

    // Here we extend / shrink our mainChildView.
    [self updateChildViewFrameRect];

    auto* windowDelegate = static_cast<WindowDelegate*>(self.delegate);
    if (nsCocoaWindow* geckoWindow = windowDelegate.geckoWidget) {
      // Re-layout our contents.
      geckoWindow->ReportSizeEvent();
    }

    // Resizing the content area causes a reflow which would send a synthesized
    // mousemove event to the old mouse position relative to the top left
    // corner of the content area. But the mouse has shifted relative to the
    // content area, so that event would have wrong position information. So
    // we'll send a mouse move event with the correct new position.
    ChildViewMouseTracker::ResendLastMouseMoveEvent();
  }
}

- (void)placeWindowButtons:(NSRect)aRect {
  if (!NSEqualRects(mWindowButtonsRect, aRect)) {
    mWindowButtonsRect = aRect;
    [self reflowTitlebarElements];
  }
}

- (NSRect)windowButtonsRect {
  return mWindowButtonsRect;
}

// Returning YES here makes the setShowsToolbarButton method work even though
// the window doesn't contain an NSToolbar.
- (BOOL)_hasToolbar {
  return YES;
}

// Dispatch a toolbar pill button clicked message to Gecko.
- (void)_toolbarPillButtonClicked:(id)sender {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  RollUpPopups();

  if ([self.delegate isKindOfClass:[WindowDelegate class]]) {
    auto* windowDelegate = static_cast<WindowDelegate*>(self.delegate);
    nsCocoaWindow* geckoWindow = windowDelegate.geckoWidget;
    if (!geckoWindow) {
      return;
    }

    if (nsIWidgetListener* listener = geckoWindow->GetWidgetListener()) {
      listener->OSToolbarButtonPressed();
    }
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Retain and release "self" to avoid crashes when our widget (and its native
// window) is closed as a result of processing a key equivalent (e.g.
// Command+w or Command+q).  This workaround is only needed for a window
// that can become key.
- (BOOL)performKeyEquivalent:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSWindow* nativeWindow = [self retain];
  BOOL retval = [super performKeyEquivalent:theEvent];
  [nativeWindow release];
  return retval;

  NS_OBJC_END_TRY_BLOCK_RETURN(NO);
}

- (void)sendEvent:(NSEvent*)anEvent {
  if (MaybeDropEventForModalWindow(anEvent, self.delegate)) {
    return;
  }
  [super sendEvent:anEvent];
}

@end

@implementation PopupWindow

- (id)initWithContentRect:(NSRect)contentRect
                styleMask:(NSUInteger)styleMask
                  backing:(NSBackingStoreType)bufferingType
                    defer:(BOOL)deferCreation {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  self = [super initWithContentRect:contentRect
                          styleMask:styleMask
                            backing:bufferingType
                              defer:deferCreation];
  if (!self) {
    return nil;
  }
  mPopover = nil;
  mPopoverViewController = nil;
  mUsePopover = NO;
  mIsContextMenu = false;

  return self;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (void)dealloc {
  if (mPopover) {
    ChildViewMouseTracker::OnDestroyWindow(
        mPopover.contentViewController.view.window);
  }

  [mPopover release];
  [mPopoverViewController release];
  [super dealloc];
}

// Override the private API _backdropBleedAmount. This determines how much the
// desktop wallpaper contributes to the vibrancy backdrop.
// Return 0 in order to match what the system does for sheet windows and
// _NSPopoverWindows.
- (CGFloat)_backdropBleedAmount {
  return 0.0;
}

// Override the private API shadowOptions.
// The constants below were found in AppKit's implementations of the
// shadowOptions method on the various window types.
static const NSUInteger kWindowShadowOptionsNoShadow = 0;
static const NSUInteger kWindowShadowOptionsMenu = 2;
static const NSUInteger kWindowShadowOptionsTooltip = 4;

- (NSDictionary*)shadowParameters {
  NSDictionary* parent = [super shadowParameters];
  // NSLog(@"%@", parent);
  if (self.shadowStyle != WindowShadow::Panel) {
    return parent;
  }
  NSMutableDictionary* copy = [parent mutableCopy];
  for (auto* key : {@"com.apple.WindowShadowInnerRimDensityActive",
                    @"com.apple.WindowShadowInnerRimDensityInactive",
                    @"com.apple.WindowShadowRimDensityActive",
                    @"com.apple.WindowShadowRimDensityInactive"}) {
    if ([parent objectForKey:key] != nil) {
      [copy setValue:@(0) forKey:key];
    }
  }
  return [copy autorelease];
}

- (NSUInteger)shadowOptions {
  if (!self.hasShadow) {
    return kWindowShadowOptionsNoShadow;
  }

  switch (self.shadowStyle) {
    case WindowShadow::None:
      return kWindowShadowOptionsNoShadow;

    case WindowShadow::Menu:
    case WindowShadow::Panel:
      return kWindowShadowOptionsMenu;

    case WindowShadow::Tooltip:
      return kWindowShadowOptionsTooltip;
  }
}

- (BOOL)isContextMenu {
  return mIsContextMenu;
}

- (void)setIsContextMenu:(BOOL)flag {
  mIsContextMenu = flag;
}

- (void)setAllowPopover {
  mUsePopover = YES;

  if (!mPopover) {
    mPopover = [[NSPopover alloc] init];

    // Use NSPopoverBehaviorApplicationDefined to prevent auto-closing
    // when other popovers are opened, and to respect the disable_autohide
    // preference
    mPopover.behavior = NSPopoverBehaviorApplicationDefined;
    mPopover.delegate = self;

    // Create view controller that will contain our content view
    mPopoverViewController = [[NSViewController alloc] init];

    NSView* contentView = self.contentView;
    if (contentView) {
      // Ensure the content view is properly configured
      [contentView
          setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

      mPopoverViewController.view = contentView;
      mPopover.contentViewController = mPopoverViewController;

      // Set popover size to match our window content size
      NSRect contentRect = [contentView frame];
      if (contentRect.size.width > 0 && contentRect.size.height > 0) {
        [mPopover setContentSize:contentRect.size];
      }
    }
  }
}

- (BOOL)usePopover {
  return mUsePopover;
}

- (void)showPopoverRelativeToRect:(NSRect)positioningRect
                           ofView:(NSView*)positioningView
                    preferredEdge:(NSRectEdge)preferredEdge
                     hiddenAnchor:(BOOL)hiddenAnchor {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;
  if (!mPopover) {
    return;
  }

  // Close existing popover if it's already shown
  if (mPopover.shown) {
    [mPopover close];
  }

  // Force content update before showing
  [self updatePopoverContent];

  if (mPopoverViewController.view) {
    mPopover.behavior = NSPopoverBehaviorApplicationDefined;

    // This is a hidden API that prevents the popover from showing its arrow
    // pointing to the anchor.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-method-access"
    [mPopover setShouldHideAnchor:hiddenAnchor];
#pragma clang diagnostic pop

    [mPopover showRelativeToRect:positioningRect
                          ofView:positioningView
                   preferredEdge:preferredEdge];
  }

  NSWindow* popoverWindow = mPopover.contentViewController.view.window;
  [popoverWindow setAcceptsMouseMovedEvents:YES];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)closePopover {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mPopover && mPopover.shown) {
    [mPopover close];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (void)updatePopoverContent {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;
  if (!mPopover || !mPopoverViewController) {
    return;
  }

  NSView* contentView = self.contentView;
  if (!contentView) {
    return;
  }
  // Ensure proper hit testing for hover events
  [contentView setWantsLayer:YES];
  [contentView setAcceptsTouchEvents:YES];

  // Update the popover content view to match current window content
  mPopoverViewController.view = contentView;

  // Update popover size to match content
  NSRect contentRect = [contentView frame];
  if (contentRect.size.width > 0 && contentRect.size.height > 0) {
    mPopover.contentSize = contentRect.size;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (NSPopover*)popover {
  return mPopover;
}

- (BOOL)canBecomeMainWindow {
  // This is overridden because the default is 'yes' when a titlebar is present.
  return NO;
}

@end

// According to Apple's docs on [NSWindow canBecomeKeyWindow] and [NSWindow
// canBecomeMainWindow], windows without a title bar or resize bar can't (by
// default) become key or main.  But if a window can't become key, it can't
// accept keyboard input (bmo bug 393250).  And it should also be possible for
// an otherwise "ordinary" window to become main.  We need to override these
// two methods to make this happen.
@implementation BorderlessWindow

- (BOOL)canBecomeKeyWindow {
  return YES;
}

- (void)sendEvent:(NSEvent*)anEvent {
  if (MaybeDropEventForModalWindow(anEvent, self.delegate)) {
    return;
  }

  [super sendEvent:anEvent];
}

// Apple's doc on this method says that the NSWindow class's default is not to
// become main if the window isn't "visible" -- so we should replicate that
// behavior here.  As best I can tell, the [NSWindow isVisible] method is an
// accurate test of what Apple means by "visibility".
- (BOOL)canBecomeMainWindow {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  return self.isVisible;

  NS_OBJC_END_TRY_BLOCK_RETURN(NO);
}

// Retain and release "self" to avoid crashes when our widget (and its native
// window) is closed as a result of processing a key equivalent (e.g.
// Command+w or Command+q).  This workaround is only needed for a window
// that can become key.
- (BOOL)performKeyEquivalent:(NSEvent*)theEvent {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSWindow* nativeWindow = [self retain];
  BOOL retval = [super performKeyEquivalent:theEvent];
  [nativeWindow release];
  return retval;

  NS_OBJC_END_TRY_BLOCK_RETURN(NO);
}

@end
